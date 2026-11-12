// ============================================================
//  BootHop - lang.cpp
//  The language files (see lang.h).
// ============================================================
#include "lang.h"
#include <algorithm>
#include <map>

namespace L
{
    using namespace wimsmith;

    wstring Code = L"en", FontFamily = L"Segoe UI";
    std::vector<wstring> Problems;

    // section -> key (lower case) -> text
    typedef std::map<wstring, std::map<wstring, wstring>> Data;
    static Data merged;
    static wstring langDir;

    void Init(const wstring& exeDir) { langDir = Combine(exeDir, L"lang"); }

    static Data Parse(const wstring& text, const wstring& source)
    {
        Data d;
        std::map<wstring, wstring>* cur = nullptr;
        std::vector<wstring> lines = SplitLines(text);
        for (size_t n = 0; n < lines.size(); n++)
        {
            wstring l = Trim(lines[n]);
            if (l.empty() || l[0] == L';' || l[0] == L'#') continue;
            if (l[0] == L'[' && l.back() == L']')
            {
                cur = &d[Lower(Trim(l.substr(1, l.size() - 2)))];
                continue;
            }
            size_t eq = l.find(L'=');
            if (eq == wstring::npos || eq == 0 || !cur)
            {
                Problems.push_back(source + L" line " + std::to_wstring(n + 1) + L": not 'key = text'");
                continue;
            }
            wstring key = Trim(l.substr(0, eq)), val = Trim(l.substr(eq + 1));
            if (!key.empty() && key.back() == L'+')
            {
                // key += text : one more line for the previous value
                key = Lower(Trim(key.substr(0, key.size() - 1)));
                auto it = cur->find(key);
                if (it != cur->end()) { it->second += L"\r\n" + val; continue; }
            }
            (*cur)[Lower(key)] = val;
        }
        return d;
    }

    // resource type LANG, name = the code in upper case (as rc stores it)
    static bool Embedded(const wstring& code, std::string& bytes)
    {
        HRSRC r = FindResourceW(nullptr, Upper(code).c_str(), L"LANG");
        if (!r) return false;
        HGLOBAL g = LoadResource(nullptr, r);
        const char* p = g ? (const char*)LockResource(g) : nullptr;
        if (!p) return false;
        bytes.assign(p, SizeofResource(nullptr, r));
        return true;
    }

    static bool ReadLanguage(const wstring& code, Data& d)
    {
        wstring f = Combine(langDir, code + L".ini");
        if (FileExists(f))
        {
            try { d = Parse(ReadTextFile(f), f); return true; }
            catch (const HelperError& e) { Problems.push_back(e.msg); }
        }
        std::string bytes;
        if (!Embedded(code, bytes)) return false;
        if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF) bytes.erase(0, 3);
        d = Parse(FromUtf8(bytes), L"embedded " + code + L".ini");
        return true;
    }

    static wstring Get(const Data& d, const wstring& section, const wstring& key)
    {
        auto s = d.find(section);
        if (s == d.end()) return wstring();
        auto k = s->second.find(Lower(key));
        return k == s->second.end() ? wstring() : k->second;
    }

    void Load(const wstring& code)
    {
        Problems.clear();
        Data en;
        if (!ReadLanguage(L"en", en)) Problems.push_back(L"en.ini was not found (embedded or in a lang folder).");
        merged = en;
        Code = L"en";
        if (!code.empty() && !EqualsI(code, L"en"))
        {
            Data other;
            if (!ReadLanguage(Lower(code), other)) Problems.push_back(code + L".ini was not found. English is used.");
            else
            {
                for (auto& sec : other)
                    for (auto& kv : sec.second) merged[sec.first][kv.first] = kv.second;
                Code = Lower(code);
            }
        }
        wstring font = Get(merged, L"meta", L"font");
        FontFamily = font.empty() ? L"Segoe UI" : font;
    }

    static BOOL CALLBACK AddName(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR lp)
    {
        if (!IS_INTRESOURCE(name)) ((std::vector<wstring>*)lp)->push_back(Lower(name));
        return TRUE;
    }

    std::vector<std::pair<wstring, wstring>> Available()
    {
        std::vector<wstring> codes;
        EnumResourceNamesW(nullptr, L"LANG", AddName, (LONG_PTR)&codes);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Combine(langDir, L"*.ini").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do codes.push_back(Lower(FileStem(fd.cFileName)));
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(codes.begin(), codes.end());
        codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
        std::vector<std::pair<wstring, wstring>> list;
        for (const wstring& c : codes)
        {
            Data d;
            wstring name;
            if (ReadLanguage(c, d)) name = Get(d, L"meta", L"name");
            list.push_back(std::make_pair(c, name.empty() ? c : name));
        }
        return list;
    }

    wstring T(const wstring& key)
    {
        auto s = merged.find(L"ui");
        if (s != merged.end())
        {
            auto k = s->second.find(Lower(key));
            if (k != s->second.end()) return k->second;
        }
        return L"[" + key + L"]";
    }

    // {0}, {1:00}; {{ and }} are braces
    wstring Format(const wstring& t, std::initializer_list<Arg> list)
    {
        std::vector<Arg> args(list);
        wstring r;
        for (size_t i = 0; i < t.size(); i++)
        {
            wchar_t c = t[i];
            if (c == L'{' && i + 1 < t.size() && t[i + 1] == L'{') { r += L'{'; i++; continue; }
            if (c == L'}' && i + 1 < t.size() && t[i + 1] == L'}') { r += L'}'; i++; continue; }
            if (c == L'{')
            {
                size_t close = t.find(L'}', i);
                if (close != wstring::npos)
                {
                    wstring spec = t.substr(i + 1, close - i - 1), fmt;
                    size_t colon = spec.find(L':');
                    if (colon != wstring::npos) { fmt = spec.substr(colon + 1); spec = spec.substr(0, colon); }
                    long long n;
                    if (TryParseLong(spec, n) && n >= 0 && n < (long long)args.size())
                    {
                        const Arg& a = args[(size_t)n];
                        wstring v = a.s;
                        // "00": at least that many digits
                        if (a.num && !fmt.empty() && fmt.find_first_not_of(L'0') == wstring::npos && a.n >= 0)
                            while (v.size() < fmt.size()) v = L"0" + v;
                        r += v;
                        i = close;
                        continue;
                    }
                }
            }
            r += c;
        }
        return r;
    }
}
