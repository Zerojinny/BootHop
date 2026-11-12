// ============================================================
//  BootHop - filedlg.cpp
//  The file and folder dialogs of ui.h (OpenFile, OpenFiles, SaveFile,
//  PickFolder) as a window of this program: no Explorer and no shell
//  dialog (IFileDialog), so they also work in a Windows PE without
//  Explorer. The folders are read with FindFirstFileEx:
//    - drives (letter, label, file system, size; network drives with
//      their \\server\share), a path field (Enter goes there, also
//      \\server\share\...), [Up] (also Backspace), [Refresh] (also F5);
//    - the list: folders first, then the files of the chosen filter;
//      name / size / date, sorted by a click on the column header;
//      double click or Enter opens a folder or takes a file;
//    - a name field, the filter, [New folder] (save / folder), OK / Cancel.
//  Folders that are hidden and system at once ($Recycle.Bin, System
//  Volume Information) are not listed. The last folder is remembered
//  while the program runs.
// ============================================================
#include "ui.h"
#include "lang.h"
#include <shellapi.h>
#include <winnetwk.h>
#include <algorithm>
#include <map>

namespace ui
{
    using wimsmith::Combine;
    using wimsmith::DirExists;
    using wimsmith::FileExists;
    using wimsmith::FileExt;
    using wimsmith::FullPath;
    using wimsmith::Lower;
    using wimsmith::Num;
    using wimsmith::Split;
    using wimsmith::Trim;
    using wimsmith::ErrorMessage;

    namespace
    {
        enum Mode { ModeOpen, ModeOpenMany, ModeSave, ModeFolder };

        struct Entry
        {
            wstring name;
            bool dir = false;
            long long size = 0;
            FILETIME modified = {};
        };

        wstring lastDir;                       // the last folder of any dialog

        // ---------- names and paths ----------

        // "*" and "?", case-insensitive
        bool Match(const wchar_t* n, const wchar_t* p)
        {
            for (; *p; p++, n++)
            {
                if (*p == L'*')
                {
                    while (p[1] == L'*') p++;
                    if (!p[1]) return true;
                    for (; *n; n++) if (Match(n, p + 1)) return true;
                    return false;
                }
                if (!*n) return false;
                if (*p != L'?' && towupper(*p) != towupper(*n)) return false;
            }
            return !*n;
        }

        // patterns "*.wim;*.esd"
        bool MatchAny(const wstring& name, const wstring& patterns)
        {
            for (const wstring& p : Split(patterns, L';'))
            {
                wstring t = Trim(p);
                if (t.empty() || t == L"*" || t == L"*.*" || Match(name.c_str(), t.c_str())) return true;
            }
            return false;
        }

        bool IsRooted(const wstring& p)
        {
            return p.compare(0, 2, L"\\\\") == 0 || (p.size() >= 2 && p[1] == L':');
        }

        // the folder above; "" at a drive root or at \\server\share
        wstring Parent(wstring p)
        {
            while (p.size() > 3 && p.back() == L'\\') p.pop_back();
            if (p.size() <= 3 && p.size() >= 2 && p[1] == L':') return wstring();
            if (p.compare(0, 2, L"\\\\") == 0)
            {
                size_t server = p.find(L'\\', 2);
                if (server == wstring::npos || p.find(L'\\', server + 1) == wstring::npos) return wstring();
            }
            size_t k = p.find_last_of(L'\\');
            if (k == wstring::npos) return wstring();
            wstring r = p.substr(0, k);
            if (r.size() == 2 && r[1] == L':') r += L'\\';
            return r;
        }

        wstring LastName(wstring p)
        {
            while (p.size() > 3 && p.back() == L'\\') p.pop_back();
            size_t k = p.find_last_of(L'\\');
            return k == wstring::npos ? p : p.substr(k + 1);
        }

        wstring SizeText(long long b)
        {
            wchar_t t[64];
            if (b < 1024) swprintf_s(t, L"%lld B", b);
            else if (b < 1048576) swprintf_s(t, L"%.0f KB", b / 1024.0);
            else if (b < 1073741824LL) swprintf_s(t, L"%.1f MB", b / 1048576.0);
            else swprintf_s(t, L"%.2f GB", b / 1073741824.0);
            return t;
        }

        wstring DateText(const FILETIME& ft)
        {
            FILETIME local;
            SYSTEMTIME s;
            if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &s)) return wstring();
            wchar_t t[32];
            swprintf_s(t, L"%04d-%02d-%02d %02d:%02d", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute);
            return t;
        }

        wstring ExeFolder()
        {
            wchar_t b[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
            wstring p = b;
            size_t k = p.find_last_of(L'\\');
            return k == wstring::npos ? wstring() : p.substr(0, k);
        }

        // ---------- icons (the system image list; shell32 is in Windows PE too) ----------

        HIMAGELIST SystemIcons()
        {
            SHFILEINFOW sfi = {};
            return (HIMAGELIST)SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof sfi,
                SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        }

        int IconOf(const wstring& name, bool dir)
        {
            static std::map<wstring, int> cache;
            wstring key = dir ? L"\\dir" : Lower(FileExt(name));
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            SHFILEINFOW sfi = {};
            wstring probe = dir ? L"folder" : L"file" + key;
            int i = SHGetFileInfoW(probe.c_str(), dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) ? sfi.iIcon : -1;
            cache[key] = i;
            return i;
        }

        // ---------- a dialog owned by another one ----------

        HWND ChildDialog(HWND owner, const wstring& title, int w, int h)
        {
            HWND d = TopWindow(title, w, h, owner, WS_POPUP | WS_CAPTION | WS_SYSMENU);
            RECT o, me;
            GetWindowRect(owner, &o);
            GetWindowRect(d, &me);
            SetWindowPos(d, nullptr, o.left + ((o.right - o.left) - (me.right - me.left)) / 2,
                o.top + ((o.bottom - o.top) - (me.bottom - me.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            OnClose(d, [d] { EndModal(d, 0); return false; });
            return d;
        }

        // one line of text; false = cancelled
        bool Prompt(HWND owner, const wstring& title, const wstring& text, wstring& value)
        {
            HWND d = ChildDialog(owner, title, 380, 120);
            HWND l = Label(d, text);
            HWND t = Edit(d);
            SetText(t, value);
            HWND ok = Button(d, L"OK");
            HWND cancel = Button(d, L::T(L"bk.btn_cancel"));
            Move(l, S(14), S(12), S(352), S(20));
            Move(t, S(14), S(38), S(352), S(24));
            Move(ok, S(186), S(80), S(86), S(28));
            Move(cancel, S(280), S(80), S(86), S(28));
            // the text is read on OK, while the field is still there (RunModal destroys the dialog)
            wstring typed;
            auto accept = [d, t, &typed] { typed = Trim(GetText(t)); EndModal(d, 1); };
            OnClick(ok, accept);
            OnClick(cancel, [d] { EndModal(d, 2); });
            OnKeys(d, accept, [d] { EndModal(d, 2); });
            SetFocus(t);
            SendMessageW(t, EM_SETSEL, 0, -1);
            bool r = RunModal(d) == 1;
            if (r) value = typed;
            return r && !value.empty();
        }

        // ---------- the dialog ----------

        struct FileDialog
        {
            Mode mode;
            Filters filters;
            bool overwritePrompt = true;
            HWND dlg = nullptr, cboDrive, txtPath, btnUp, btnRefresh, list, lblName, txtName, cboFilter;
            HWND btnNew, lblStatus, btnOk, btnCancel;
            wstring cur;
            std::vector<Entry> rows;
            std::vector<wstring> drives;       // "C:\" ... in the order of cboDrive
            int sortColumn = 0;
            bool sortDown = false;
            wstring autoName;                  // the name field as the list set it
            std::vector<wstring> result;

            FileDialog(Mode m, const Filters& f) : mode(m), filters(f)
            {
                if (filters.empty() && mode != ModeFolder) filters.push_back({ L::T(L"fd.all_files"), L"*.*" });
            }

            wstring Patterns() const
            {
                int i = mode == ModeFolder ? -1 : ComboIndex(cboFilter);
                return i >= 0 && i < (int)filters.size() ? filters[i].second : L"*";
            }

            // ".wim" of the chosen filter (else of the first), "" for "*"
            wstring DefaultExt() const
            {
                int i = ComboIndex(cboFilter);
                for (int k : { i, 0 })
                {
                    if (k < 0 || k >= (int)filters.size()) continue;
                    wstring p = Trim(Split(filters[k].second, L';')[0]);
                    if (p.size() > 2 && p.compare(0, 2, L"*.") == 0 && p.find_first_of(L"*?", 2) == wstring::npos) return p.substr(1);
                }
                return wstring();
            }

            void Create(const wstring& title)
            {
                dlg = Dialog(title, 760, 500);
                cboDrive = Combo(dlg);
                txtPath = Edit(dlg);
                btnUp = Button(dlg, L::T(L"fd.up"));
                btnRefresh = Button(dlg, L::T(L"adk.btn_refresh"));
                list = List(dlg, { { L::T(L"br.col_name"), 380 }, { L::T(L"br.col_size"), -100 }, { L::T(L"br.col_modified"), 150 } },
                    LVS_OWNERDATA | (mode == ModeOpenMany ? 0 : LVS_SINGLESEL));
                HIMAGELIST icons = SystemIcons();
                if (icons) ListView_SetImageList(list, icons, LVSIL_SMALL);
                lblName = Label(dlg, L::T(mode == ModeFolder ? L"fd.folder" : L"fd.name"), FontBold, color::Dim, SS_LEFT | SS_CENTERIMAGE);
                txtName = Edit(dlg);
                cboFilter = Combo(dlg);
                for (const auto& f : filters) ComboAdd(cboFilter, f.first);
                if (!filters.empty()) SendMessageW(cboFilter, CB_SETCURSEL, 0, 0);
                btnNew = Button(dlg, L::T(L"fd.new_folder"));
                lblStatus = Label(dlg, L"", FontUI, color::Dim, SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE);
                btnOk = Button(dlg, L::T(mode == ModeSave ? L"fd.ok_save" : mode == ModeFolder ? L"fd.ok_folder" : L"fd.ok_open"));
                Primary(btnOk, false);
                btnCancel = Button(dlg, L::T(L"bk.btn_cancel"));
                Show(cboFilter, mode != ModeFolder);
                Show(btnNew, mode == ModeSave || mode == ModeFolder);

                // laid out for the size of the window: 760 x 500, smaller on a small screen
                SetLayout(dlg, [this](int w, int h) { Layout(w, h); });
                Relayout(dlg);

                FillDrives();
                Connect();
            }

            void Layout(int w, int h)
            {
                int r = w - S(14);
                int dw = std::min(S(240), std::max(S(110), (w - S(28)) / 3));
                Move(cboDrive, S(14), S(12), dw, S(23));
                Move(btnRefresh, r - S(96), S(10), S(96), S(28));
                Move(btnUp, r - S(96) - S(8) - S(70), S(10), S(70), S(28));
                int px = S(14) + dw + S(8);
                Move(txtPath, px, S(12), std::max(0, r - S(96 + 8 + 70 + 8) - px), S(23));
                Move(list, S(14), S(46), std::max(0, w - S(28)), std::max(S(40), h - S(96) - S(46)));
                int y1 = h - S(84), y2 = h - S(44);
                Move(lblName, S(14), y1, S(80), S(23));
                int fw = mode == ModeFolder ? 0 : std::min(S(192), std::max(S(100), (w - S(28)) / 4));
                Move(txtName, S(96), y1, std::max(0, r - (fw ? fw + S(8) : 0) - S(96)), S(23));
                if (fw) Move(cboFilter, r - fw, y1, fw, S(23));
                Move(btnNew, S(14), y2, S(110), S(28));
                Move(btnCancel, r - S(90), y2, S(90), S(28));
                Move(btnOk, r - S(90) - S(6) - S(90), y2, S(90), S(28));
                int sx = S(mode == ModeSave || mode == ModeFolder ? 134 : 14);
                Move(lblStatus, sx, y2 + S(2), std::max(0, r - S(186) - S(8) - sx), S(24));
            }

            // ---- drives ----

            void FillDrives()
            {
                drives.clear();
                ComboClear(cboDrive);
                wchar_t buf[512] = {};
                DWORD n = GetLogicalDriveStringsW(511, buf);
                for (const wchar_t* d = buf; n && *d; d += wcslen(d) + 1)
                {
                    wstring root = d;
                    UINT type = GetDriveTypeW(root.c_str());
                    wstring text = root.substr(0, 2);
                    if (type == DRIVE_REMOTE)
                    {
                        // a network drive is not asked for its label (an offline server would hang the dialog)
                        wchar_t unc[512] = {};
                        DWORD un = 511;
                        if (WNetGetConnectionW(root.substr(0, 2).c_str(), unc, &un) == NO_ERROR) text += L"  " + wstring(unc);
                    }
                    else
                    {
                        wchar_t label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
                        if (GetVolumeInformationW(root.c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, fs, MAX_PATH + 1))
                        {
                            if (label[0]) text += L"  " + wstring(label);
                            ULARGE_INTEGER total = {};
                            wstring info = fs;
                            if (GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, nullptr) && total.QuadPart)
                                info += (info.empty() ? L"" : L", ") + SizeText((long long)total.QuadPart);
                            if (!info.empty()) text += L"  (" + info + L")";
                        }
                        else if (type == DRIVE_CDROM || type == DRIVE_REMOVABLE) text += L"  " + L::T(L"fd.no_media");
                    }
                    drives.push_back(root);
                    ComboAdd(cboDrive, text);
                }
            }

            void SyncDrive()
            {
                int pick = -1;
                for (size_t i = 0; i < drives.size(); i++)
                    if (cur.size() >= 2 && towupper(cur[0]) == towupper(drives[i][0]) && cur[1] == L':') pick = (int)i;
                SendMessageW(cboDrive, CB_SETCURSEL, (WPARAM)pick, 0);
            }

            // ---- the folder ----

            void SetStatus(const wstring& t, bool error)
            {
                SetText(lblStatus, t);
                SetColor(lblStatus, error ? color::Danger : color::Dim);
            }

            // false: the folder was not read (the old one stays)
            bool Load(const wstring& dir, const wstring& select = wstring())
            {
                wstring d = IsRooted(dir) ? FullPath(dir) : wstring();
                if (d.size() == 2 && d[1] == L':') d += L'\\';
                if (d.empty()) { SetStatus(L::F(L"fd.no_folder", dir), true); return false; }
                std::vector<Entry> found;
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileExW(Combine(d, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
                if (h == INVALID_HANDLE_VALUE)
                {
                    DWORD e = GetLastError();
                    if (e != ERROR_FILE_NOT_FOUND)
                    {
                        SetStatus(L::F(L"fd.read_failed", d, ErrorMessage(e)), true);
                        SyncDrive();
                        return false;
                    }
                }
                else
                {
                    wstring patterns = Patterns();
                    do
                    {
                        wstring n = fd.cFileName;
                        if (n == L"." || n == L"..") continue;
                        DWORD a = fd.dwFileAttributes;
                        if ((a & FILE_ATTRIBUTE_HIDDEN) && (a & FILE_ATTRIBUTE_SYSTEM)) continue;
                        Entry e;
                        e.name = n;
                        e.dir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        if (!e.dir && (mode == ModeFolder || !MatchAny(n, patterns))) continue;
                        e.size = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                        e.modified = fd.ftLastWriteTime;
                        found.push_back(e);
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                cur = d;
                lastDir = d;
                rows.swap(found);
                Sort();
                SetText(txtPath, cur);
                SyncDrive();
                Enable(btnUp, !Parent(cur).empty());
                long long dirs = 0;
                for (const Entry& e : rows) if (e.dir) dirs++;
                SetStatus(L::F(L"br.folder_count", dirs, (long long)rows.size() - dirs), false);
                if (mode == ModeFolder) { SetText(txtName, cur); autoName = cur; }
                if (!select.empty())
                    for (size_t i = 0; i < rows.size(); i++)
                        if (wimsmith::EqualsI(rows[i].name, select))
                        {
                            ListView_SetItemState(list, (int)i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                            ListView_EnsureVisible(list, (int)i, FALSE);
                            break;
                        }
                return true;
            }

            void Sort()
            {
                int col = sortColumn;
                bool down = sortDown;
                std::stable_sort(rows.begin(), rows.end(), [col, down](const Entry& a, const Entry& b)
                {
                    if (a.dir != b.dir) return a.dir;
                    int c = 0;
                    if (col == 1 && !a.dir) c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
                    else if (col == 2) c = CompareFileTime(&a.modified, &b.modified);
                    if (c == 0)
                        c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                            a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) - CSTR_EQUAL;
                    return down ? c > 0 : c < 0;
                });
                ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemCountEx(list, (int)rows.size(), 0);
                InvalidateRect(list, nullptr, TRUE);
                if (!rows.empty()) ListView_EnsureVisible(list, 0, FALSE);
            }

            std::vector<int> Selected() const
            {
                std::vector<int> s;
                for (int i = ListView_GetNextItem(list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list, i, LVNI_SELECTED))
                    if (i < (int)rows.size()) s.push_back(i);
                return s;
            }

            void Up()
            {
                wstring p = Parent(cur);
                if (!p.empty()) Load(p, LastName(cur));
            }

            // a typed name: relative to the folder shown
            wstring Resolve(const wstring& t) const
            {
                wstring p = IsRooted(t) ? t : Combine(cur, t);
                if (p.size() == 2 && p[1] == L':') p += L'\\';
                return FullPath(p);
            }

            // ---- the list ----

            void SelectionChanged()
            {
                std::vector<int> s = Selected();
                if (mode == ModeFolder)
                {
                    autoName = s.size() == 1 && rows[s[0]].dir ? Combine(cur, rows[s[0]].name) : cur;
                    SetText(txtName, autoName);
                    return;
                }
                std::vector<wstring> files;
                for (int i : s) if (!rows[i].dir) files.push_back(rows[i].name);
                if (files.empty()) return;
                if (files.size() == 1) autoName = files[0];
                else
                {
                    autoName.clear();
                    for (const wstring& f : files) autoName += (autoName.empty() ? L"\"" : L" \"") + f + L"\"";
                }
                SetText(txtName, autoName);
            }

            // double click / Enter on a row
            void Activate()
            {
                std::vector<int> s = Selected();
                if (s.size() == 1 && rows[s[0]].dir) { Load(Combine(cur, rows[s[0]].name)); return; }
                if (!s.empty()) Accept();
            }

            void NewFolder()
            {
                wstring name = L::T(L"fd.new_folder_name");
                if (!Prompt(dlg, L::T(L"fd.new_folder"), L::T(L"fd.new_folder_prompt"), name)) return;
                wstring p = Combine(cur, name);
                if (!CreateDirectoryW(p.c_str(), nullptr))
                {
                    Info(L::F(L"fd.new_folder_failed", p, ErrorMessage(GetLastError())), MB_ICONWARNING);
                    return;
                }
                Load(cur, name);
                SetFocus(list);
            }

            // ---- OK ----

            void Done(const std::vector<wstring>& r)
            {
                result = r;
                EndModal(dlg, 1);
            }

            void Accept()
            {
                wstring t = Trim(GetText(txtName));
                if (mode == ModeFolder)
                {
                    wstring p = t.empty() ? cur : Resolve(t);
                    if (!p.empty() && DirExists(p)) Done({ p });
                    else Info(L::F(L"fd.no_folder", t), MB_ICONWARNING);
                    return;
                }
                // several files chosen in the list
                if (mode == ModeOpenMany && !t.empty() && t == autoName)
                {
                    std::vector<wstring> files;
                    for (int i : Selected()) if (!rows[i].dir) files.push_back(Combine(cur, rows[i].name));
                    if (!files.empty()) { Done(files); return; }
                }
                if (t.empty()) return;
                wstring p = Resolve(t);
                if (p.empty()) { Info(L::F(L"fd.not_found", t), MB_ICONWARNING); return; }
                if (DirExists(p)) { Load(p); SetText(txtName, L""); return; }
                if (mode != ModeSave)
                {
                    if (FileExists(p)) Done({ p });
                    else Info(L::F(L"fd.not_found", p), MB_ICONWARNING);
                    return;
                }
                if (FileExt(p).empty()) p += DefaultExt();
                wstring dir = wimsmith::DirName(p);
                if (!DirExists(dir)) { Info(L::F(L"fd.no_folder", dir), MB_ICONWARNING); return; }
                if (FileExists(p) && overwritePrompt && !YesNo(L::F(L"fd.overwrite", p), MB_ICONWARNING)) return;
                Done({ p });
            }

            // Enter: in the path field it goes there, on the list it opens the row
            void EnterKey()
            {
                HWND f = GetFocus();
                if (f == txtPath) { wstring p = Trim(GetText(txtPath)); Load(IsRooted(p) ? p : Combine(cur, p)); return; }
                if (f == list) { Activate(); return; }
                if (f == btnCancel) { EndModal(dlg, 0); return; }
                if (f == btnUp) { Up(); return; }
                if (f == btnRefresh) { Load(cur); return; }
                if (f == btnNew) { NewFolder(); return; }
                Accept();
            }

            void Connect()
            {
                OnChange(cboDrive, [this]
                {
                    int i = ComboIndex(cboDrive);
                    if (i >= 0 && i < (int)drives.size()) Load(drives[i]);
                });
                OnChange(cboFilter, [this] { Load(cur); });
                OnClick(btnUp, [this] { Up(); });
                OnClick(btnRefresh, [this] { Load(cur); });
                OnClick(btnNew, [this] { NewFolder(); });
                OnClick(btnOk, [this] { Accept(); });
                OnClick(btnCancel, [this] { EndModal(dlg, 0); });
                OnChange(list, [this] { SelectionChanged(); });
                OnListDoubleClick(list, [this] { Activate(); });
                OnListNotify(list, [this](NMHDR* n, LRESULT& r) -> bool
                {
                    switch (n->code)
                    {
                    case LVN_GETDISPINFOW:
                    {
                        NMLVDISPINFOW* d = (NMLVDISPINFOW*)n;
                        int i = d->item.iItem;
                        if (i < 0 || i >= (int)rows.size()) return true;
                        const Entry& e = rows[i];
                        if (d->item.mask & LVIF_TEXT)
                        {
                            wstring t = d->item.iSubItem == 0 ? e.name : d->item.iSubItem == 1 ? (e.dir ? wstring() : SizeText(e.size))
                                : DateText(e.modified);
                            wcsncpy_s(d->item.pszText, d->item.cchTextMax, t.c_str(), _TRUNCATE);
                        }
                        if (d->item.mask & LVIF_IMAGE) d->item.iImage = IconOf(e.name, e.dir);
                        r = 0;
                        return true;
                    }
                    case LVN_COLUMNCLICK:
                    {
                        NMLISTVIEW* c = (NMLISTVIEW*)n;
                        sortDown = c->iSubItem == sortColumn ? !sortDown : false;
                        sortColumn = c->iSubItem;
                        Sort();
                        return true;
                    }
                    case LVN_KEYDOWN:
                    {
                        NMLVKEYDOWN* k = (NMLVKEYDOWN*)n;
                        if (k->wVKey == VK_BACK) Up();
                        else if (k->wVKey == VK_F5) Load(cur);
                        return true;
                    }
                    }
                    return false;
                });
                OnKeys(dlg, [this] { EnterKey(); }, [this] { EndModal(dlg, 0); });
            }

            // the folder to start in: the given one, else the last one, else the program's
            void Start(const wstring& initialDir)
            {
                for (const wstring& d : { initialDir, lastDir, ExeFolder(), wstring(L"C:\\") })
                    if (!d.empty() && DirExists(d) && Load(d)) return;
                if (!drives.empty()) Load(drives[0]);
            }

            bool Run()
            {
                SetFocus(mode == ModeFolder ? list : txtName);
                return RunModal(dlg) == 1 && !result.empty();
            }
        };
    }

    bool OpenFile(const Filters& filters, const wstring& initialDir, wstring& file)
    {
        FileDialog d(ModeOpen, filters);
        d.Create(L::T(L"fd.title_open"));
        d.Start(initialDir);
        if (!d.Run()) return false;
        file = d.result[0];
        return true;
    }

    bool OpenFiles(const Filters& filters, const wstring& initialDir, std::vector<wstring>& files)
    {
        FileDialog d(ModeOpenMany, filters);
        d.Create(L::T(L"fd.title_open_many"));
        d.Start(initialDir);
        files.clear();
        if (!d.Run()) return false;
        files = d.result;
        return true;
    }

    bool SaveFile(const Filters& filters, const wstring& initialDir, const wstring& fileName, bool overwritePrompt, wstring& file)
    {
        FileDialog d(ModeSave, filters);
        d.overwritePrompt = overwritePrompt;
        d.Create(L::T(L"fd.title_save"));
        d.Start(initialDir);
        SetText(d.txtName, fileName);
        if (!d.Run()) return false;
        file = d.result[0];
        return true;
    }

    bool PickFolder(const wstring& title, const wstring& initialDir, wstring& folder)
    {
        FileDialog d(ModeFolder, Filters());
        d.Create(title.empty() ? L::T(L"fd.title_folder") : title);
        d.Start(initialDir);
        if (!d.Run()) return false;
        folder = d.result[0];
        return true;
    }

    bool InputText(const wstring& title, const wstring& prompt, wstring& value)
    {
        return Prompt(Main, title, prompt, value);
    }
}
