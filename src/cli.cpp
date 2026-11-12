// ============================================================
//  BootHop - cli.cpp
//  The command line (cli.h).
// ============================================================
#include "cli.h"
#include "lang.h"
#include "settings.h"
#include "ramboot.h"

namespace wimsmith
{
    static HANDLE outHandle = nullptr, errHandle = nullptr;

    // the console: its own (BootHop.com), else the one of the program that
    // started it (BootHop.exe from cmd); a redirected stdout / stderr stays
    static void OpenConsole()
    {
        outHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        errHandle = GetStdHandle(STD_ERROR_HANDLE);
        if (outHandle && outHandle != INVALID_HANDLE_VALUE && errHandle && errHandle != INVALID_HANDLE_VALUE) return;
        if (!GetConsoleWindow() && !AttachConsole(ATTACH_PARENT_PROCESS)) return;
        HANDLE con = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (con == INVALID_HANDLE_VALUE) return;
        if (!outHandle || outHandle == INVALID_HANDLE_VALUE) outHandle = con;
        if (!errHandle || errHandle == INVALID_HANDLE_VALUE) errHandle = con;
    }

    void Print(const wstring& text, bool error)
    {
        HANDLE h = error ? errHandle : outHandle;
        if (!h || h == INVALID_HANDLE_VALUE) return;
        wstring t = text + L"\r\n";
        DWORD mode = 0, n = 0;
        if (GetConsoleMode(h, &mode)) { WriteConsoleW(h, t.c_str(), (DWORD)t.size(), &n, nullptr); return; }
        // a file or a pipe: the code page of the console, UTF-8 without one
        UINT cp = GetConsoleOutputCP();
        std::string b = ToCodePage(t, cp ? cp : CP_UTF8);
        WriteFile(h, b.data(), (DWORD)b.size(), &n, nullptr);
    }

    // the language of the console texts by its code page only (BootHop.ini is
    // not read for a command): Korean where the console shows Hangul (949,
    // Johab 1361), English on the others; UTF-8 (65001) shows both: the
    // language of Windows (loaded by main.cpp)
    static void PickLanguage()
    {
        UINT cp = GetConsoleOutputCP();
        if (cp == 0 || cp == CP_UTF8) return;   // no console / UTF-8: as loaded
        wstring code = cp == 949 || cp == 1361 ? L"ko" : L"en";
        if (!EqualsI(code, L::Code)) L::Load(code);
    }

    // cli.usage: the language files lose the spaces at the start of a line,
    // so the lines below a command line ("BootHop ...") are indented here
    // up to the next empty line
    static int Usage(bool wrong)
    {
        wstring text;
        bool under = false;
        for (const wstring& l : SplitLines(L::T(L"cli.usage")))
        {
            if (l.empty()) under = false;
            else if (l.compare(0, 8, L"BootHop ") == 0) { text += l + L"\r\n"; under = true; continue; }
            text += (under ? L"    " : L"") + l + L"\r\n";
        }
        Print(TrimEnd(text), wrong);
        return wrong ? 2 : 0;
    }

    bool RunCli(const std::vector<wstring>& args, int& rc)
    {
        if (args.empty() || args[0].empty() || args[0][0] != L'/') return false;
        OpenConsole();
        PickLanguage();
        wstring cmd = Lower(args[0]);
        if (cmd == L"/?" || cmd == L"/help") { rc = Usage(false); return true; }
        if (cmd == L"/status" || cmd == L"/cancel")
        {
            rc = args.size() == 1 ? RunBootState(cmd == L"/cancel") : Usage(true);
            return true;
        }
        if (cmd != L"/boot") { Print(L::F(L"cli.unknown", args[0]), true); rc = Usage(true); return true; }

        // /boot <file> [/index N] [/restart] [/copysdi [/deletesdi]]. The command
        // line is used on its own (without BootHop.exe): it reads no setting the
        // window saves and no BootHop.ini; boot.sdi is the one of Windows unless
        // /copysdi is given, a copied one is deleted after the restart with /deletesdi
        BootOptions o;
        wstring file;
        for (size_t n = 1; n < args.size(); n++)
        {
            wstring a = Lower(args[n]);
            if (a == L"/restart") o.restart = true;
            else if (a == L"/copysdi") o.copySdi = true;
            else if (a == L"/deletesdi") o.deleteSdi = true;
            else if (a == L"/index" && n + 1 < args.size())
            {
                o.index = _wtoi(args[++n].c_str());
                if (o.index <= 0) { Print(L::F(L"cli.bad_index", args[n]), true); rc = 2; return true; }
            }
            else if (!a.empty() && a[0] != L'/' && file.empty()) file = args[n];
            else { Print(L::F(L"cli.unknown", args[n]), true); rc = Usage(true); return true; }
        }
        if (file.empty()) { rc = Usage(true); return true; }
        wstring full = FullPath(file);
        if (!full.empty()) file = full;
        if (!FileExists(file)) { Print(L::F(L"ob.file_missing", file), true); rc = 1; return true; }
        rc = RunOneTimeBootQuiet(file, o);
        return true;
    }
}
