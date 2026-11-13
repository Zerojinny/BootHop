// ============================================================
//  BootHop - ramboot.cpp
//  Boot once from a WIM file (ramboot.h); taken from WimSmith.
// ============================================================
#include "ramboot.h"
#include "cli.h"
#include "lang.h"
#include "settings.h"
#include "ui.h"
#include "wimg.h"
#include <shellapi.h>
#include <winternl.h>

namespace wimsmith
{
    using namespace ui;

    // the command line (/boot): no window and no question; the texts go to
    // the console (cli.cpp Print), a question counts as "yes"
    static const BootOptions* quiet = nullptr;

    static void Tell(const wstring& text, UINT icon)
    {
        if (quiet) Print(text, icon == MB_ICONERROR || icon == MB_ICONWARNING);
        else Info(text, icon);
    }

    static bool Ask(const wstring& text, UINT icon)
    {
        if (!quiet) return YesNo(text, icon);
        if (icon == MB_ICONWARNING) Print(text, true);   // a warning is still shown
        return true;
    }

    wstring RunShutdown(bool restart)
    {
        wstring args = wstring(restart ? L"/r" : L"/s") + L" /t 0 /d p:0:0";
        try
        {
            std::string out, err;
            int rc = RunProcess(Combine(SystemDir(), L"shutdown.exe"), args, out, err);
            if (rc != 0) return Trim(FromCodePage(out + err, CP_OEMCP)) + L" (" + Num(rc) + L")";
            return wstring();
        }
        catch (const HelperError& e) { return e.msg; }
    }

    // boot.sdi copied by the helper, deleted after the PC has started again
    // (delete_sdi = 1): a value under Run of the user starts
    // "BootHop.exe --cleanup <sdi> <boot time>" at each logon until
    // the boot time differs, i.e. the PC has restarted (the one-time start
    // from the WIM is then over). A logoff / logon or a fast startup keeps
    // the boot time, so the file stays until a real restart.
    static const wchar_t* RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static const wchar_t* CleanupValue = L"BootHop.DeleteBootSdi";

    // the time this Windows started (100 ns since 1601), 0 = unknown
    static unsigned long long BootTime()
    {
        struct TimeOfDay { LARGE_INTEGER BootTime, CurrentTime, TimeZoneBias; ULONG TimeZoneId, Reserved; ULONGLONG BootTimeBias, SleepTimeBias; };
        typedef LONG(NTAPI* QuerySystemInformation)(int, void*, ULONG, ULONG*);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        QuerySystemInformation query = ntdll ? (QuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation") : nullptr;
        TimeOfDay t = {};
        if (!query || query(3 /* SystemTimeOfDayInformation */, &t, sizeof t, nullptr) < 0) return 0;
        return (unsigned long long)t.BootTime.QuadPart;
    }

    bool DeleteSdiAfterBoot() { return DeleteSdiSetting(); }

    // the start this program put into the boot sequence, for the list window:
    // HKCU\Software\BootHop, PendingFile + PendingBootTime (the boot time of
    // Windows when it was made). The boot manager uses the boot sequence at the
    // next start, so after a restart (another boot time) it is over.
    static const wchar_t* const StateKey = L"Software\\BootHop";

    static void SetPendingBoot(const wstring& file)
    {
        wstring boot = std::to_wstring(BootTime());
        RegSetKeyValueW(HKEY_CURRENT_USER, StateKey, L"PendingFile", REG_SZ, file.c_str(), (DWORD)((file.size() + 1) * sizeof(wchar_t)));
        RegSetKeyValueW(HKEY_CURRENT_USER, StateKey, L"PendingBootTime", REG_SZ, boot.c_str(), (DWORD)((boot.size() + 1) * sizeof(wchar_t)));
    }

    static void ClearPendingBoot()
    {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, StateKey, L"PendingFile");
        RegDeleteKeyValueW(HKEY_CURRENT_USER, StateKey, L"PendingBootTime");
    }

    wstring PendingBoot()
    {
        wchar_t file[2048], boot[64];
        DWORD a = sizeof file, b = sizeof boot;
        if (RegGetValueW(HKEY_CURRENT_USER, StateKey, L"PendingFile", RRF_RT_REG_SZ, nullptr, file, &a) != ERROR_SUCCESS
            || RegGetValueW(HKEY_CURRENT_USER, StateKey, L"PendingBootTime", RRF_RT_REG_SZ, nullptr, boot, &b) != ERROR_SUCCESS)
            return wstring();
        unsigned long long then = wcstoull(boot, nullptr, 10), now = BootTime();
        unsigned long long diff = now > then ? now - then : then - now;
        if (now != 0 && diff >= 10ULL * 10000000ULL) { ClearPendingBoot(); return wstring(); }   // restarted since
        return file;
    }

    static void ScheduleSdiCleanup(const wstring& sdi)
    {
        unsigned long long boot = BootTime();
        if (boot == 0) return;
        wstring cmd = L"\"" + GuiExe() + L"\" --cleanup \"" + sdi + L"\" " + std::to_wstring(boot);
        RegSetKeyValueW(HKEY_CURRENT_USER, RunKey, CleanupValue, REG_SZ, cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    }

    int RunSdiCleanup(const wstring& sdi, const wstring& bootTime)
    {
        unsigned long long then = wcstoull(bootTime.c_str(), nullptr, 10), now = BootTime();
        if (now == 0) return 1;
        // the same start (some seconds apart when the clock was set): later again
        unsigned long long diff = now > then ? now - then : then - now;
        if (then != 0 && diff < 10ULL * 10000000ULL) return 0;
        // only a file named boot.sdi; it may be gone already
        if (EqualsI(FileName(sdi), L"boot.sdi") && FileExists(sdi)) DeleteFileW(sdi.c_str());
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RunKey, CleanupValue);
        return 0;
    }

    static wstring Gb(long long bytes)
    {
        wchar_t b[64];
        swprintf_s(b, L"%.1f GB", bytes / 1073741824.0);
        return b;
    }

    // n bytes of a file from offset (from the end when offset < 0); "" = not read
    static std::string ReadAt(const wstring& file, long long offset, DWORD n)
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return std::string();
        std::string b(n, '\0');
        LARGE_INTEGER at;
        at.QuadPart = offset;
        DWORD got = 0;
        bool ok = SetFilePointerEx(h, at, nullptr, offset < 0 ? FILE_END : FILE_BEGIN) && ReadFile(h, &b[0], n, &got, nullptr) && got == n;
        CloseHandle(h);
        return ok ? b : std::string();
    }

    // the header of the file before anything else reads it (wimgapi only says
    // ERROR_BAD_FORMAT, e.g. for an empty file: seen 2026-11-12):
    // .wim "MSWIM\0\0\0" (also a split part / ESD), .vhdx "vhdxfile",
    // .vhd the footer "conectix" in its last 512 bytes (511 in old ones).
    // "" = it looks right, else the language key of the problem
    static const wchar_t* BadHeader(const wstring& file)
    {
        long long size = FileSize(file);
        if (size <= 0) return L"rb.empty_file";
        wstring ext = FileExt(file);
        if (EqualsI(ext, L".wim")) return ReadAt(file, 0, 8) == std::string("MSWIM\0\0\0", 8) ? nullptr : L"rb.bad_header";
        if (EqualsI(ext, L".vhdx")) return ReadAt(file, 0, 8) == "vhdxfile" ? nullptr : L"rb.bad_header";
        if (EqualsI(ext, L".vhd"))
        {
            if (size < 512) return L"rb.bad_header";
            std::string tail = ReadAt(file, -512, 512);
            return tail.find("conectix") != std::string::npos ? nullptr : L"rb.bad_header";
        }
        return nullptr;
    }

    static wstring TypeName(const wstring& file)
    {
        wstring t = FileExt(file);
        if (!t.empty()) t = t.substr(1);
        for (wchar_t& c : t) c = (wchar_t)towupper(c);
        return t;
    }

    // the image to start: a list when the file has more than one (the boot image first chosen)
    static int ChooseBootImage(const wstring& wim, const wg::WimFileInfo& f)
    {
        HWND d = Dialog(L::T(L"rb.title"), 520, 160);
        HWND l = Label(d, L::F(L"rb.choose", wim));
        HWND cbo = Combo(d);
        int sel = 0;
        for (size_t n = 0; n < f.Images.size(); n++)
        {
            const wg::WimImage& im = f.Images[n];
            wstring text = Num(im.Index) + L": " + im.Name;
            if (!im.InstallationType.empty()) text += L" (" + im.InstallationType + L")";
            if (im.Index == f.BootIndex) { text += L"  [" + L::T(L"rb.boot_image") + L"]"; sel = (int)n; }
            ComboAdd(cbo, text);
        }
        ComboSelect(cbo, sel);
        HWND ok = Button(d, L"OK"), cancel = Button(d, L::T(L"bk.btn_cancel"));
        Primary(ok, false);
        SetLayout(d, [=](int cw, int ch)
        {
            Move(l, S(14), S(12), cw - S(28), S(40));
            Move(cbo, S(14), S(56), cw - S(28), S(24));
            int by = ch - S(14) - S(28);
            Move(ok, cw - S(14) - S(86) * 2 - S(8), by, S(86), S(28));
            Move(cancel, cw - S(14) - S(86), by, S(86), S(28));
        });
        Relayout(d);
        int chosen = 0;
        auto take = [&, d] { int i = ComboIndex(cbo); if (i >= 0) chosen = f.Images[i].Index; EndModal(d, 1); };
        OnClick(ok, take);
        OnClick(cancel, [d] { EndModal(d, 0); });
        OnKeys(d, take, [d] { EndModal(d, 0); });
        SetFocus(cbo);
        return RunModal(d) == 1 ? chosen : 0;
    }

    // the UAC prompt was cancelled: nothing to tell in the window, an exit code on the command line
    static int Cancelled()
    {
        if (!quiet) return 0;
        Print(L::T(L"cli.uac_cancelled"), true);
        return (int)ERROR_CANCELLED;
    }

    // the elevated helper with job.ini in a new work folder; a small window while
    // it runs. false = not started (UAC cancelled: nothing to tell)
    // working: the language key of the line while it runs
    static bool RunElevated(const Job& values, wstring& workDir, DWORD& code, const wchar_t* working = L"rb.working")
    {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        workDir = Combine(Combine(tmp, L"BootHop_job"), stamp);
        CreateDirs(workDir);
        wstring text;
        for (const auto& kv : values) text += kv.first + L" = " + kv.second + L"\r\n";
        if (!WriteBytes(Combine(workDir, L"job.ini"), ToUtf8(text))) throw HelperError{ L"job.ini: " + ErrorMessage(GetLastError()) };

        // BootHop.exe when it is there (the helper needs no console). Without it
        // BootHop.com itself: the class comfile has no "runas" (ERROR_NO_ASSOCIATION),
        // so the "runas" of exefile ("%1" %*) is used for it (SEE_MASK_CLASSNAME);
        // its console is hidden (SW_HIDE)
        wstring self = GuiExe();
        wstring args = L"--helper \"" + workDir + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof sei };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        if (EqualsI(FileExt(self), L".com")) { sei.fMask |= SEE_MASK_CLASSNAME; sei.lpClass = L"exefile"; }
        sei.lpVerb = L"runas";
        sei.lpFile = self.c_str();
        sei.lpParameters = args.c_str();
        sei.lpDirectory = workDir.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei) || !sei.hProcess)
        {
            DWORD e = GetLastError();
            DeleteTree(workDir);
            if (e == ERROR_CANCELLED) return false;
            throw HelperError{ ErrorMessage(e) };
        }
        HANDLE process = sei.hProcess;
        if (quiet)
        {
            Print(L::T(working), false);
            WaitForSingleObject(process, INFINITE);
            code = (DWORD)-1;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            return true;
        }
        HWND d = Dialog(L::T(L"rb.title"), 420, 90);
        SetWindowLongPtrW(d, GWL_EXSTYLE, GetWindowLongPtrW(d, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        HWND l = Label(d, L::T(working));
        HWND bar = Progress(d);
        Move(l, S(14), S(14), S(392), S(24));
        Move(bar, S(14), S(50), S(392), S(6));
        ProgressMarquee(bar, true);
        ui::SetTimer(d, 1, 250, [d, process] { if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) EndModal(d, 1); });
        RunModal(d);
        ui::KillTimer(d, 1);
        DestroyWindow(d);
        WaitForSingleObject(process, INFINITE);   // the window closed early: the job still ends
        code = (DWORD)-1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        return true;
    }

    // status.txt without the progress lines (the last ones)
    static wstring StatusText(const wstring& workDir)
    {
        std::string bytes;
        if (!ReadBytes(Combine(workDir, L"status.txt"), bytes)) return wstring();
        std::vector<wstring> lines;
        for (const wstring& l : SplitLines(FromUtf8(bytes)))
            if (!l.empty() && l.compare(0, 2, L"P ") != 0) lines.push_back(l.compare(0, 5, L"EXIT ") == 0 ? l.substr(5) : l);
        wstring r;
        for (size_t n = lines.size() > 12 ? lines.size() - 12 : 0; n < lines.size(); n++) r += lines[n] + L"\r\n";
        return TrimEnd(r);
    }

    // the end of a successful job: the work folder deleted, "restart now?"
    static void Succeeded(const wstring& file, const wstring& workDir)
    {
        SetPendingBoot(file);
        try
        {
            DeleteTree(workDir);
            wstring parent = DirName(workDir);
            if (!DirHasEntries(parent)) RemoveDirectoryW(parent.c_str());
        }
        catch (const HelperError&) {}
        if (quiet)
        {
            Print(L::F(L"cli.done", file), false);
            if (!quiet->restart) return;
            wstring e = RunShutdown(true);
            if (!e.empty()) Print(L::F(L"bk.after_failed", L::T(L"bk.after_restart"), e), true);
            return;
        }
        if (YesNo(L::F(L"rb.done", file), MB_ICONQUESTION))
        {
            wstring e = RunShutdown(true);
            if (!e.empty()) Info(L::F(L"bk.after_failed", L::T(L"bk.after_restart"), e), MB_ICONERROR);
        }
    }

    // a .vhd / .vhdx: Windows in it started natively once (helper mode vhdboot:
    // the VHD attached read-only, its Windows partition found, an osloader on
    // it in the boot sequence); no wimgapi, no boot.sdi
    static int RunVhdBoot(const wstring& vhd)
    {
        wstring root = Root(vhd);
        wstring fs = root.size() == 3 ? DriveFileSystem(root[0]) : wstring();
        if (!EqualsI(fs, L"NTFS")) { Tell(L::F(L"vb.not_ntfs", vhd, fs.empty() ? wstring(L"-") : fs), MB_ICONWARNING); return 1; }
        // Windows does not support native VHD boot from a BitLocker drive
        if (BitLockerOn(root[0]) && !Ask(L::F(L"vb.bitlocker", vhd, root.substr(0, 2)), MB_ICONWARNING)) return 0;
        if (!Ask(L::F(L"vb.confirm", vhd, Gb(FileSize(vhd))), MB_ICONQUESTION)) return 0;
        wstring workDir;
        DWORD code = 0;
        try
        {
            if (!RunElevated({ { L"mode", L"vhdboot" }, { L"vhd", vhd }, { L"name", FileStem(vhd) } }, workDir, code)) return Cancelled();
        }
        catch (const HelperError& e) { Tell(L::F(L"rb.failed", wstring(L"-"), e.msg, workDir), MB_ICONERROR); return 1; }
        if (code != 0)
        {
            Tell(L::F(L"rb.failed", (long long)(int)code, StatusText(workDir), workDir), MB_ICONERROR);
            return (int)code;
        }
        Succeeded(vhd, workDir);
        return 0;
    }

    int RunOneTimeBoot(const wstring& wim)
    {
        wstring ext = FileExt(wim);
        bool vhd = EqualsI(ext, L".vhd") || EqualsI(ext, L".vhdx");
        if (!vhd && !EqualsI(ext, L".wim")) { Tell(L::F(L"rb.not_wim", wim), MB_ICONWARNING); return 1; }
        wstring root = Root(wim);
        UINT type = root.size() == 3 ? DriveTypeOf(root[0]) : DRIVE_REMOTE;
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) { Tell(L::F(L"rb.not_local", wim), MB_ICONWARNING); return 1; }
        if (!FileExists(wim)) { Tell(L::F(L"ob.file_missing", wim), MB_ICONWARNING); return 1; }
        if (const wchar_t* bad = BadHeader(wim)) { Tell(L::F(bad, wim, TypeName(wim)), MB_ICONWARNING); return 1; }
        if (vhd) return RunVhdBoot(wim);

        // wimgapi.dll of Windows (wimg.cpp); reading needs no elevation
        wstring err = wg::Load();
        if (!err.empty()) { Tell(L::F(L"ob.no_wimgapi", err), MB_ICONWARNING); return 1; }
        wg::WimFileInfo f;
        err = wg::ReadInfo(wim, f);
        if (!err.empty()) { Tell(L::F(L"bk.read_failed", wim, err), MB_ICONWARNING); return 1; }
        if (f.TotalParts > 1) { Tell(L::F(L"rb.split", wim), MB_ICONWARNING); return 1; }
        if (f.Images.empty()) { Tell(L::F(L"rb.no_image", wim), MB_ICONWARNING); return 1; }
        int index = 0;
        if (!quiet) index = f.Images.size() == 1 ? f.Images[0].Index : ChooseBootImage(wim, f);
        else if (quiet->index > 0) index = quiet->index;
        else if (f.Images.size() == 1) index = f.Images[0].Index;
        else if (f.BootIndex > 0) index = f.BootIndex;
        else { Print(L::F(L"cli.need_index", wim, (long long)f.Images.size()), true); return 2; }
        if (index <= 0) return 0;
        const wg::WimImage* im = nullptr;
        for (const wg::WimImage& i : f.Images) if (i.Index == index) im = &i;
        if (!im) { if (quiet) Print(L::F(L"cli.no_index", wim, index), true); return 1; }
        bool setBoot = f.BootIndex != index;
        long long size = f.FileBytes > 0 ? f.FileBytes : FileSize(wim);

        // the loader of this firmware in the image: checked here already, before
        // the UAC prompt (the helper checks it again); unelevated it works too.
        // Only Windows PE starts from a RAM disk: INSTALLATIONTYPE "WindowsPE", or
        // else the shell of Windows PE in the image (winpeshl.exe: a PE built from
        // install.wim, like PhoenixPE, keeps "Client" in its XML; a regular
        // Windows has no winpeshl.exe)
        {
            bool pe = EqualsI(im->InstallationType, L"WindowsPE");
            bool uefi = FirmwareUefi();
            wstring loader = uefi ? L"\\Windows\\System32\\Boot\\winload.efi" : L"\\Windows\\System32\\Boot\\winload.exe";
            wchar_t tmp[MAX_PATH + 1];
            GetTempPathW(MAX_PATH + 1, tmp);
            wstring temp = Combine(Combine(tmp, L"BootHop_check"), Num((long long)GetCurrentProcessId()));
            bool missing = false;
            try
            {
                CreateDirs(temp);
                wg::HasFile(wim, index, loader, temp, &missing);
                if (!pe && !missing) pe = wg::HasFile(wim, index, L"\\Windows\\System32\\winpeshl.exe", temp).empty();
                DeleteTree(temp);
                wstring parent = DirName(temp);
                if (!DirHasEntries(parent)) RemoveDirectoryW(parent.c_str());
            }
            catch (const HelperError&) {}
            if (missing) { Tell(L::F(L"rb.no_loader", wim, index, loader, uefi ? wstring(L"UEFI") : wstring(L"BIOS")), MB_ICONWARNING); return 1; }
            if (!pe && !Ask(L::F(L"rb.not_pe", index, im->Name, im->InstallationType.empty() ? wstring(L"-") : im->InstallationType), MB_ICONWARNING))
                return 0;
        }
        // the boot manager reads the WIM from the drive: an encrypted one may fail
        if (BitLockerOn(root[0]) && !Ask(L::F(L"rb.bitlocker", wim, root.substr(0, 2)), MB_ICONWARNING)) return 0;
        // the whole WIM is loaded into memory, and Windows PE needs about 1 GB more
        MEMORYSTATUSEX mem = { sizeof mem };
        if (GlobalMemoryStatusEx(&mem) && (unsigned long long)size + 1073741824ULL > mem.ullTotalPhys
            && !Ask(L::F(L"rb.low_memory", wim, Gb(size), Gb((long long)mem.ullTotalPhys)), MB_ICONWARNING))
            return 0;
        // boot.sdi: the one of Windows (copy_sdi = 0, the default) where the boot
        // manager can read it, else next to the WIM (copied when missing); the
        // helper decides the same way (job key copy_sdi)
        // the command line: only its /copysdi; the window: its saved setting
        bool wantCopy = quiet ? quiet->copySdi : CopySdiSetting();
        bool wantDelete = quiet ? quiet->deleteSdi : DeleteSdiAfterBoot();   // the window: delete_sdi of BootHop.ini
        bool sdiNextToWim = wantCopy || SystemBootSdi().empty();
        wstring sdi = Combine(DirName(wim), L"boot.sdi");
        wstring text = L::F(L"rb.confirm", wim, index, im->Name, Gb(size));
        if (setBoot) text += L"\r\n\r\n" + (f.BootIndex > 0 ? L::F(L"rb.confirm_set_boot", f.BootIndex, index) : L::F(L"rb.confirm_set_boot_none", index));
        bool copySdi = sdiNextToWim && !FileExists(sdi);
        if (copySdi) text += L"\r\n\r\n" + L::F(L"rb.confirm_sdi", sdi) + (wantDelete ? L"\r\n" + L::T(L"rb.confirm_sdi_delete") : wstring());
        if (!Ask(text, MB_ICONQUESTION)) return 0;

        wstring workDir;
        DWORD code = 0;
        try
        {
            if (!RunElevated({ { L"mode", L"ramboot" }, { L"wim", wim }, { L"index", Num(index) },
                { L"set_boot", setBoot ? L"1" : L"0" }, { L"name", im->Name }, { L"copy_sdi", wantCopy ? L"1" : L"0" } }, workDir, code))
                return Cancelled();
        }
        catch (const HelperError& e) { Tell(L::F(L"rb.failed", wstring(L"-"), e.msg, workDir), MB_ICONERROR); return 1; }
        if (code != 0)
        {
            Tell(L::F(L"rb.failed", (long long)(int)code, StatusText(workDir), workDir), MB_ICONERROR);
            return (int)code;
        }
        if (copySdi && FileExists(sdi) && wantDelete) ScheduleSdiCleanup(sdi);
        Succeeded(wim, workDir);
        return 0;
    }

    int RunOneTimeBootQuiet(const wstring& file, const BootOptions& o)
    {
        quiet = &o;
        int rc = RunOneTimeBoot(file);
        quiet = nullptr;
        return rc;
    }

    int RunBootState(bool cancel, bool window)
    {
        BootOptions o;
        if (!window) quiet = &o;
        // the texts: on the console, or (the window) in one message box at the end
        wstring shown;
        bool shownError = false;
        auto say = [&](const wstring& t, bool error)
        {
            if (!window) { Print(t, error); return; }
            shown += t + L"\r\n";
            shownError = shownError || error;
        };
        auto show = [&] { if (window && !shown.empty()) Info(TrimEnd(shown), shownError ? MB_ICONERROR : MB_ICONINFORMATION); };
        wstring workDir;
        DWORD code = 0;
        int rc = 0;
        try
        {
            if (!RunElevated({ { L"mode", cancel ? L"cancel" : L"status" } }, workDir, code, cancel ? L"cli.cancelling" : L"cli.checking")) { rc = Cancelled(); quiet = nullptr; return rc; }
        }
        catch (const HelperError& e) { quiet = nullptr; say(e.msg, true); show(); return 1; }
        quiet = nullptr;
        // STATE pending|entry <id> <description>
        std::string bytes;
        ReadBytes(Combine(workDir, L"status.txt"), bytes);
        std::vector<wstring> pending, leftover;   // the descriptions
        for (const wstring& l : SplitLines(FromUtf8(bytes)))
        {
            if (l.compare(0, 6, L"STATE ") != 0) continue;
            wstring rest = l.substr(6);
            size_t a = rest.find(L' '), b = a == wstring::npos ? a : rest.find(L' ', a + 1);
            wstring desc = L"  " + (b == wstring::npos ? wstring() : rest.substr(b + 1));
            (rest.compare(0, 8, L"pending ") == 0 ? pending : leftover).push_back(desc);
        }
        if (code != 0 && code != 1)
        {
            say(L::F(L"rb.failed", (long long)(int)code, StatusText(workDir), workDir), true);
            show();
            return (int)code;
        }
        try { DeleteTree(workDir); } catch (const HelperError&) {}
        if (cancel)
        {
            ClearPendingBoot();
            say(pending.empty() && leftover.empty() ? L::T(L"cli.cancel_none") : L::T(L"cli.cancelled"), false);
            for (const wstring& d : pending) say(d, false);
            for (const wstring& d : leftover) say(d, false);
            show();
            return 0;
        }
        if (pending.empty()) ClearPendingBoot();
        Print(pending.empty() ? L::T(L"cli.not_pending") : L::T(L"cli.pending"), false);
        for (const wstring& d : pending) Print(d, false);
        // an entry the boot manager has used already (or one taken out of the
        // boot sequence): it stays in the BCD until the next job or /cancel
        if (!leftover.empty())
        {
            Print(L::T(L"cli.leftover"), false);
            for (const wstring& d : leftover) Print(d, false);
        }
        return pending.empty() ? 1 : 0;
    }
}
