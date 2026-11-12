// ============================================================
//  BootHop - lang.h
//  All text on the screen comes from the language files (as in the C#
//  version): <exe folder>\lang\<code>.ini first, else the copy embedded
//  in the exe (resource type LANG, name = code; cpp\build.bat embeds
//  every lang\*.ini). en.ini is the reference: anything missing in
//  another language is taken from it.
//  Format (UTF-8): [meta] name, font; [ui] key = text; "key += text"
//  adds a line; {0}, {1} ... ({2:00}: two digits) are filled in.
// ============================================================
#pragma once
#include "common.h"
#include <initializer_list>

namespace L
{
    using std::wstring;

    extern wstring Code, FontFamily;
    extern std::vector<wstring> Problems;

    void Init(const wstring& exeDir);
    void Load(const wstring& code);
    // code -> display name ([meta] name) of every language
    std::vector<std::pair<wstring, wstring>> Available();

    // an argument of F(): text or a number
    struct Arg
    {
        wstring s;
        bool num = false;
        long long n = 0;
        Arg(const wstring& v) : s(v) {}
        Arg(const wchar_t* v) : s(v ? v : L"") {}
        Arg(wchar_t c) : s(1, c) {}
        Arg(int v) : s(std::to_wstring(v)), num(true), n(v) {}
        Arg(unsigned v) : s(std::to_wstring(v)), num(true), n(v) {}
        Arg(long v) : s(std::to_wstring(v)), num(true), n(v) {}
        Arg(long long v) : s(std::to_wstring(v)), num(true), n(v) {}
        Arg(unsigned long long v) : s(std::to_wstring(v)), num(true), n((long long)v) {}
    };

    // text of [ui] key; "[key]" when it is missing everywhere
    wstring T(const wstring& key);
    wstring Format(const wstring& text, std::initializer_list<Arg> args);
    template <class... A> wstring F(const wstring& key, A... a) { return Format(T(key), { Arg(a)... }); }
}
