// ============================================================
//  BootHop - bootlist.cpp
//  The list of files to boot once from (bootlist.h).
// ============================================================
#include "bootlist.h"
#include "lang.h"
#include "menu.h"
#include "ramboot.h"
#include "settings.h"
#include "ui.h"
#include <commctrl.h>
#include <shellapi.h>
#include <functional>

namespace wimsmith
{
    using namespace ui;

    static wstring ListFile() { return Combine(ExeDir(), L"BootHop.list"); }

    static std::vector<wstring> LoadList()
    {
        std::vector<wstring> files;
        wstring f = ListFile();
        if (!FileExists(f)) return files;
        try
        {
            for (const wstring& l : SplitLines(ReadTextFile(f)))
            {
                wstring t = Trim(l);
                if (t.empty() || t[0] == L';') continue;
                bool seen = false;
                for (const wstring& x : files) if (EqualsI(x, t)) seen = true;
                if (!seen) files.push_back(t);
            }
        }
        catch (const HelperError&) {}
        return files;
    }

    // "" = saved, else the error
    static wstring SaveList(const std::vector<wstring>& files)
    {
        wstring text;
        for (const wstring& f : files) text += f + L"\r\n";
        if (WriteBytes(ListFile(), "\xEF\xBB\xBF" + ToUtf8(text))) return wstring();
        return ErrorMessage(GetLastError());
    }

    static wstring SizeText(long long bytes)
    {
        wchar_t b[64];
        if (bytes >= 1073741824LL) swprintf_s(b, L"%.1f GB", bytes / 1073741824.0);
        else swprintf_s(b, L"%.0f MB", bytes / 1048576.0);
        return b;
    }

    static wstring TypeText(const wstring& file)
    {
        wstring ext = FileExt(file);
        wstring t = ext.size() > 1 ? ext.substr(1) : ext;
        for (wchar_t& c : t) c = (wchar_t)towupper(c);
        return t;
    }

    // the control that had the focus gets it back when the window is active
    // again (after a message box, a dialog, another program): the top windows
    // of ui.cpp leave the focus on themselves
    static HWND lastFocus;
    // the open list window is marked with this property; WM_COPYDATA with
    // CopyAdd carries a path (UTF-16 with its 0) to add to it
    static const wchar_t* const ListProp = L"BootHop.List";
    static const ULONG_PTR CopyAdd = 0x4F42414E;
    static std::function<void(const wstring&)> addFromMenu;
    // files dropped on the window from Explorer (WM_DROPFILES)
    static std::function<void(const std::vector<wstring>&)> addDropped;

    static BOOL CALLBACK FindListProc(HWND h, LPARAM lp)
    {
        if (!GetPropW(h, ListProp)) return TRUE;
        *(HWND*)lp = h;
        return FALSE;
    }

    static HWND FindListWindow()
    {
        HWND found = nullptr;
        EnumWindows(FindListProc, (LPARAM)&found);
        return found;
    }

    static bool BootableExt(const wstring& file)
    {
        wstring ext = FileExt(file);
        return EqualsI(ext, L".wim") || EqualsI(ext, L".vhd") || EqualsI(ext, L".vhdx");
    }

    static LRESULT CALLBACK KeepFocus(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (m == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE)
        {
            HWND f = GetFocus();
            if (f && IsChild(h, f)) lastFocus = f;
        }
        else if (m == WM_COPYDATA)
        {
            // "BootHop.exe --add <file>" of another process (FindListWindow)
            const COPYDATASTRUCT* c = (const COPYDATASTRUCT*)lp;
            if (c && c->dwData == CopyAdd && c->lpData && addFromMenu)
            {
                wstring path((const wchar_t*)c->lpData, c->cbData / sizeof(wchar_t));
                while (!path.empty() && path.back() == 0) path.pop_back();
                if (!path.empty()) addFromMenu(path);
                return TRUE;
            }
        }
        else if (m == WM_DROPFILES)
        {
            HDROP drop = (HDROP)wp;
            std::vector<wstring> paths;
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT n = 0; n < count; n++)
            {
                UINT len = DragQueryFileW(drop, n, nullptr, 0);
                wstring p(len + 1, L'\0');
                DragQueryFileW(drop, n, &p[0], len + 1);
                p.resize(len);
                paths.push_back(p);
            }
            DragFinish(drop);
            if (addDropped && !paths.empty()) { SetForegroundWindow(h); addDropped(paths); }
            return 0;
        }
        else if (m == WM_SETFOCUS && lastFocus && IsWindow(lastFocus) && IsWindowEnabled(lastFocus))
        {
            SetFocus(lastFocus);
            return 0;
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    int RunBootList(const wstring& addFile)
    {
        if (!addFile.empty() && !BootableExt(addFile)) { Info(L::F(L"rb.not_wim", addFile), MB_ICONWARNING); return 1; }
        // one list window: several files chosen in Explorer start a process
        // each; the first one opens the window, the others hand their file to it
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\BootHop.List");
        if (mutex) WaitForSingleObject(mutex, 10000);
        auto release = [&mutex] { if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); mutex = nullptr; } };
        if (HWND other = FindListWindow())
        {
            if (!addFile.empty())
            {
                COPYDATASTRUCT c = { CopyAdd, (DWORD)((addFile.size() + 1) * sizeof(wchar_t)), (void*)addFile.c_str() };
                DWORD_PTR r = 0;
                SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&c, SMTO_NORMAL, 10000, &r);
            }
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(GetLastActivePopup(other));
            release();
            return 0;
        }

        std::vector<wstring> files = LoadList();

        HWND w = TopWindow(L"BootHop", 760, 420, nullptr);
        Main = w;
        SetMinSize(w, 720, 300);   // the three check boxes in a row above the buttons
        HWND hint = Label(w, L::T(L"ob.hint"), nullptr, color::Dim);
        // the start made last, while it is pending (ramboot.cpp PendingBoot), in place of the hint
        HWND pendingLabel = Label(w, L"", nullptr, color::Accent);
        HWND btnCancel = Button(w, L::T(L"ob.cancel_boot"));
        Tooltip(btnCancel, L::T(L"ob.cancel_boot_tip"));
        HWND list = List(w, { { L::T(L"ob.col_name"), 220 }, { L::T(L"ob.col_type"), 60 }, { L::T(L"ob.col_size"), -90 },
            { L::T(L"ob.col_folder"), 340 } });
        HWND btnAdd = Button(w, L::T(L"ob.add")), btnRemove = Button(w, L::T(L"ob.remove"));
        HWND btnBoot = Button(w, L::T(L"ob.boot")), btnClose = Button(w, L::T(L"ob.close"));
        Primary(btnBoot, false);
        // the right-click menu of Explorer on / off (menu.cpp)
        HWND chkMenu = Check(w, L::T(L"ob.menu_on"));
        SetChecked(chkMenu, ContextMenuOn());
        Tooltip(chkMenu, L::T(L"ob.menu_on_tip"));
        // boot.sdi copied next to the WIM, or the one of Windows (settings.cpp)
        HWND chkSdi = Check(w, L::T(L"ob.copy_sdi"));
        SetChecked(chkSdi, CopySdiSetting());
        Tooltip(chkSdi, L::T(L"ob.copy_sdi_tip"));
        // a copied boot.sdi deleted after the restart (settings.cpp DeleteSdiSetting); only
        // of use when one is copied: the box above on, or no usable one of Windows
        HWND chkDel = Check(w, L::T(L"ob.delete_sdi"));
        SetChecked(chkDel, DeleteSdiSetting());
        Tooltip(chkDel, L::T(L"ob.delete_sdi_tip"));
        bool sdiOfWindows = !SystemBootSdi().empty();
        auto updateDel = [=] { Enable(chkDel, Checked(chkSdi) || !sdiOfWindows); };
        updateDel();
        auto showPending = [=]
        {
            wstring p = PendingBoot();
            if (!p.empty()) SetText(pendingLabel, L::F(L"ob.pending", FileName(p), DirName(p)));
            Show(hint, p.empty());
            Show(pendingLabel, !p.empty());
            Show(btnCancel, !p.empty());
        };

        auto update = [=] { bool on = ListSelected(list) >= 0; Enable(btnRemove, on); Enable(btnBoot, on); };
        // the rows again from files; sel: the row chosen after it
        auto fill = [=, &files](int sel)
        {
            ListClear(list);
            for (const wstring& f : files)
            {
                long long size = FileExists(f) ? FileSize(f) : -1;
                ListAdd(list, { FileName(f), TypeText(f), size >= 0 ? SizeText(size) : L::T(L"ob.missing"), DirName(f) });
            }
            if (!files.empty())
            {
                int i = std::max(0, std::min(sel, (int)files.size() - 1));
                ListSelect(list, i);
                ListView_SetItemState(list, i, LVIS_FOCUSED, LVIS_FOCUSED);   // the arrow keys go on from it
            }
            update();
        };
        auto save = [&files]
        {
            wstring e = SaveList(files);
            if (!e.empty()) Info(L::F(L"ob.save_failed", ListFile(), e), MB_ICONWARNING);
        };

        // paths into the list (each once), saved; the last one chosen. false = none taken
        auto addPaths = [&, list](const std::vector<wstring>& paths)
        {
            int last = -1;
            bool added = false;
            for (const wstring& p : paths)
            {
                if (!BootableExt(p)) { Info(L::F(L"rb.not_wim", p), MB_ICONWARNING); continue; }
                int at = -1;
                for (size_t n = 0; n < files.size(); n++) if (EqualsI(files[n], p)) at = (int)n;
                if (at < 0) { files.push_back(p); at = (int)files.size() - 1; added = true; }
                last = at;
            }
            if (last < 0) return false;
            if (added) save();
            fill(last);
            return true;
        };
        auto add = [&, list]
        {
            int sel = ListSelected(list);
            wstring dir = sel >= 0 && sel < (int)files.size() ? DirName(files[sel]) : ExeDir();
            std::vector<wstring> picked;
            if (!OpenFiles({ { L::T(L"ob.wim_files"), L"*.wim;*.vhd;*.vhdx" }, { L::T(L"fd.all_files"), L"*.*" } }, dir, picked)) return;
            if (addPaths(picked)) SetFocus(list);
        };
        auto remove = [&, list]
        {
            int sel = ListSelected(list);
            if (sel < 0 || sel >= (int)files.size()) return;
            files.erase(files.begin() + sel);
            save();
            fill(sel);
            SetFocus(list);
        };
        auto boot = [&, list]
        {
            int sel = ListSelected(list);
            if (sel < 0 || sel >= (int)files.size()) return;
            wstring f = files[sel];
            if (!FileExists(f)) { Info(L::F(L"ob.file_missing", f), MB_ICONWARNING); return; }
            RunOneTimeBoot(f);
            fill(sel);   // the size may have changed (boot index written)
            showPending();
        };
        auto cancelBoot = [=]
        {
            RunBootState(true, true);
            showPending();
        };

        SetLayout(w, [=](int cw, int ch)
        {
            int m = S(14), bh = S(28), bw = S(100);
            // the top row: the hint, or the pending start with [Cancel] at the right
            Move(hint, m, S(14), cw - 2 * m, S(22));
            Move(btnCancel, cw - m - bw, S(10), bw, bh);
            Move(pendingLabel, m, S(14), cw - 2 * m - bw - S(8), S(22));
            int by = ch - m - bh;
            // the options in a row of their own between the list and the buttons
            int oy = by - S(8) - S(22);
            int top = S(10) + bh + S(8);
            Move(list, m, top, cw - 2 * m, oy - S(8) - top);
            int x = m;
            Move(chkMenu, x, oy, CheckWidth(chkMenu), S(22));
            x += CheckWidth(chkMenu) + S(24);
            Move(chkSdi, x, oy, CheckWidth(chkSdi), S(22));
            x += CheckWidth(chkSdi) + S(24);
            Move(chkDel, x, oy, CheckWidth(chkDel), S(22));
            Move(btnAdd, m, by, bw, bh);
            Move(btnRemove, m + bw + S(8), by, bw, bh);
            Move(btnClose, cw - m - bw, by, bw, bh);
            Move(btnBoot, cw - m - bw * 2 - S(8), by, bw, bh);
        });
        OnClick(btnAdd, add);
        OnClick(btnRemove, remove);
        OnClick(btnBoot, boot);
        OnClick(btnClose, [w] { DestroyWindow(w); });
        OnClick(chkMenu, [chkMenu]
        {
            bool on = Checked(chkMenu);
            if (!SetContextMenu(on)) { Info(L::T(L"ob.menu_failed"), MB_ICONWARNING); SetChecked(chkMenu, ContextMenuOn()); }
        });
        OnClick(chkSdi, [chkSdi, updateDel]
        {
            if (!SetCopySdiSetting(Checked(chkSdi))) { Info(L::T(L"ob.setting_failed"), MB_ICONWARNING); SetChecked(chkSdi, CopySdiSetting()); }
            updateDel();
        });
        OnClick(chkDel, [chkDel]
        {
            if (!SetDeleteSdiSetting(Checked(chkDel))) { Info(L::T(L"ob.setting_failed"), MB_ICONWARNING); SetChecked(chkDel, DeleteSdiSetting()); }
        });
        OnClick(btnCancel, cancelBoot);
        OnChange(list, update);
        OnListDoubleClick(list, boot);
        OnListNotify(list, [=](NMHDR* n, LRESULT&) -> bool
        {
            if (n->code != LVN_KEYDOWN) return false;
            WORD key = ((NMLVKEYDOWN*)n)->wVKey;
            if (key == VK_DELETE) remove();
            else if (key == VK_INSERT) add();
            else return false;
            return true;
        });
        OnKeys(w, boot, [w] { DestroyWindow(w); });
        OnDestroy(w, [w] { RemovePropW(w, ListProp); PostQuitMessage(0); });

        SetWindowSubclass(w, KeepFocus, 1, 0);
        addFromMenu = [&](const wstring& p) { addPaths({ p }); };
        addDropped = [&, list](const std::vector<wstring>& p) { if (addPaths(p)) SetFocus(list); };
        DragAcceptFiles(w, TRUE);
        showPending();
        Relayout(w);
        if (addFile.empty() || !addPaths({ addFile })) fill(0);
        SetPropW(w, ListProp, (HANDLE)1);
        ShowWindow(w, SW_SHOW);
        SetForegroundWindow(w);
        release();
        lastFocus = files.empty() ? btnAdd : list;
        SetFocus(files.empty() ? btnAdd : list);
        int rc = MessageLoop();
        Main = nullptr;
        lastFocus = nullptr;
        addFromMenu = nullptr;
        addDropped = nullptr;
        return rc;
    }
}
