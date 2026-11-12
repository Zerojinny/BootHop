// ============================================================
//  BootHop - main.cpp
//  Entry point:
//    BootHop.exe <file.wim>              boot once from it (ramboot.cpp)
//    BootHop.exe --add <file>            adds it to the list (bootlist.cpp)
//    BootHop.exe                         the list of files to boot once
//                                         from (bootlist.cpp)
//    BootHop.exe --helper <work folder>  the elevated helper (helper.cpp)
//    BootHop.exe --cleanup <sdi> <time>  at logon: deletes the copied
//                                         boot.sdi after a restart
//    BootHop /boot | /status | /cancel | /menu   the command line (cli.cpp)
//  Each start of the GUI brings the right-click menu items of
//  .wim / .vhd / .vhdx up to date when it is on (menu.cpp).
// ============================================================
#include "bootlist.h"
#include "cli.h"
#include "helper.h"
#include "lang.h"
#include "menu.h"
#include "ramboot.h"
#include "settings.h"
#include "ui.h"
#include <shellapi.h>
#include <objbase.h>

using namespace wimsmith;

// without a choice: Korean on a Korean Windows, else English
static std::wstring DefaultLanguage()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ? L"ko" : L"en";
}

// BootHop.com started without a command prompt (Explorer, the right-click
// menu, the Run value at logon: BootHop.exe is not there): the console
// Windows made for it alone is let go, so no empty console window stays
// open. Started from cmd (other processes on the console): it stays.
static void DropOwnConsole()
{
    DWORD ids[2];
    if (GetConsoleWindow() && GetConsoleProcessList(ids, 2) == 1) FreeConsole();
}

// the earlier name of the program (OnceBoot, until 2026-11-12): its list, its
// ini, its setting (HKCU\Software\OnceBoot) and a pending boot.sdi cleanup
// (a Run value with the old exe) are taken over once by the window program;
// the right-click menu by UpdateContextMenu (menu.cpp)
static void TakeOverOldName()
{
    for (const wchar_t* ext : { L".list", L".ini" })
    {
        std::wstring from = Combine(ExeDir(), std::wstring(L"OnceBoot") + ext), to = Combine(ExeDir(), std::wstring(L"BootHop") + ext);
        if (FileExists(from) && !FileExists(to)) MoveFileW(from.c_str(), to.c_str());
    }
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OnceBoot", 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD v = 0, size = sizeof v, have = 0, haveSize = sizeof have;
        bool old = RegQueryValueExW(k, L"CopySdi", nullptr, nullptr, (BYTE*)&v, &size) == ERROR_SUCCESS;
        RegCloseKey(k);
        if (old && RegGetValueW(HKEY_CURRENT_USER, L"Software\\BootHop", L"CopySdi", RRF_RT_REG_DWORD, nullptr, &have, &haveSize) != ERROR_SUCCESS)
            SetCopySdiSetting(v == 1);
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\OnceBoot");
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\OnceBoot");
    }
    // "<old exe>" --cleanup "<sdi>" <time>: the same with this exe under the new name
    const wchar_t* run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    wchar_t b[2048];
    DWORD size = sizeof b;
    if (RegGetValueW(HKEY_CURRENT_USER, run, L"OnceBoot.DeleteBootSdi", RRF_RT_REG_SZ, nullptr, b, &size) == ERROR_SUCCESS)
    {
        std::wstring cmd = b;
        size_t at = cmd.find(L"\" --cleanup ");
        if (at != std::wstring::npos)
        {
            cmd = L"\"" + GuiExe() + cmd.substr(at);
            RegSetKeyValueW(HKEY_CURRENT_USER, run, L"BootHop.DeleteBootSdi", REG_SZ, cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        }
        RegDeleteKeyValueW(HKEY_CURRENT_USER, run, L"OnceBoot.DeleteBootSdi");
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    // no "insert a disk" boxes of Windows for empty drives
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int n = 1; n < argc; n++) args.push_back(argv[n]);
        LocalFree(argv);
    }
    if (!args.empty() && args[0] == L"--helper") return RunHelper(args);
    if (args.size() >= 3 && args[0] == L"--cleanup") { DropOwnConsole(); return RunSdiCleanup(args[1], args[2]); }

    // the file given is relative to the folder it was started from
    bool add = args.size() >= 2 && args[0] == L"--add";
    if (add) args.erase(args.begin());
    std::wstring wim;
    if (!args.empty()) { wim = FullPath(args[0]); if (wim.empty()) wim = args[0]; }

    // a command ("/..."): no BootHop.ini at all (the command line is used on its
    // own; its language follows the code page of the console, cli.cpp)
    bool command = !args.empty() && !args[0].empty() && args[0][0] == L'/';
    if (!command) TakeOverOldName();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ui::Init(inst);
    L::Init(ExeDir());
    L::Load(command ? DefaultLanguage() : GetSetting(L"language", DefaultLanguage()));
    if (!command) ui::SetTheme(!EqualsI(GetSetting(L"theme", L"dark"), L"light"));
    ui::InitFonts(L::FontFamily);
    if (!L::Problems.empty())
    {
        std::wstring text;
        for (const std::wstring& p : L::Problems) text += p + L"\r\n";
        MessageBoxW(nullptr, text.c_str(), L"BootHop", MB_OK | MB_ICONWARNING);
    }
    int rc = 0;
    if (RunCli(args, rc)) { CoUninitialize(); return rc; }
    DropOwnConsole();
    UpdateContextMenu();
    rc = add ? RunBootList(wim) : wim.empty() ? RunBootList() : RunOneTimeBoot(wim);
    CoUninitialize();
    return rc;
}
