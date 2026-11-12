// ============================================================
//  BootHop - menu.cpp
//  The right-click menu items of .wim / .vhd / .vhdx (menu.h).
// ============================================================
#include "menu.h"
#include "lang.h"
#include "settings.h"
#include <shlobj.h>

namespace wimsmith
{
    static const wchar_t* const MenuExtensions[] = { L".wim", L".vhd", L".vhdx" };

    // the items: the key under shell, the text, the arguments before "%1"
    struct Verb { const wchar_t* key; const wchar_t* text; const wchar_t* args; };
    static const Verb Verbs[] = {
        { L"BootHop", L"ob.menu", L"" },
        { L"BootHop.Add", L"ob.menu_add", L"--add " },
    };

    // the menu of the empty space in a folder window; Windows shows it on the desktop too
    static const wchar_t* const BackgroundKey = L"Software\\Classes\\Directory\\Background\\shell\\BootHop.List";

    static wstring MenuKey(const wstring& ext, const wchar_t* verb)
    {
        return L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\shell\\" + verb;
    }

    static wstring Self() { return GuiExe(); }

    static wstring Read(const wstring& key, const wchar_t* value)
    {
        wchar_t b[2048];
        DWORD size = sizeof b;
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value, RRF_RT_REG_SZ, nullptr, b, &size) != ERROR_SUCCESS) return L"";
        return b;
    }

    static LSTATUS Write(const wstring& key, const wchar_t* value, const wstring& data)
    {
        return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), value, REG_SZ, data.c_str(), (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    }

    static bool KeyExists(const wstring& key)
    {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
        RegCloseKey(k);
        return true;
    }

    bool ContextMenuOn()
    {
        return KeyExists(MenuKey(L".wim", Verbs[0].key)) || KeyExists(BackgroundKey);
    }

    // every item made / brought up to date (want), or removed; false = a key not written
    static bool Apply(bool want)
    {
        wstring self = Self(), icon = L"\"" + self + L"\",0";
        bool changed = false, ok = true;
        // one item: made / brought up to date when wanted, else removed
        auto item = [&](const wstring& key, const wstring& text, const wstring& command)
        {
            if (!want)
            {
                if (KeyExists(key) && RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) == ERROR_SUCCESS)
                {
                    RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
                    changed = true;
                }
                return;
            }
            // only when something differs (another place of the exe, another language)
            if (Read(key, L"MUIVerb") == text && Read(key, L"Icon") == icon && Read(key + L"\\command", nullptr) == command) return;
            RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
            if (Write(key, L"MUIVerb", text) != ERROR_SUCCESS || Write(key, L"Icon", icon) != ERROR_SUCCESS
                || Write(key + L"\\command", nullptr, command) != ERROR_SUCCESS)
                ok = false;
            changed = true;
        };
        for (const wchar_t* ext : MenuExtensions)
            for (const Verb& v : Verbs)
                item(MenuKey(ext, v.key), L::T(v.text), L"\"" + self + L"\" " + v.args + L"\"%1\"");
        // the empty space of a folder (also of the desktop): the list window
        item(BackgroundKey, L::T(L"ob.menu_list"), L"\"" + self + L"\"");
        if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return ok && ContextMenuOn() == want;
    }

    // the items of the earlier name (OnceBoot): removed; the menu stays on when they were there
    static bool RemoveOldMenu()
    {
        bool had = false;
        auto drop = [&](const wstring& key)
        {
            if (KeyExists(key) && RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) == ERROR_SUCCESS)
            {
                RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
                had = true;
            }
        };
        for (const wchar_t* ext : MenuExtensions)
        {
            drop(MenuKey(ext, L"OnceBoot"));
            drop(MenuKey(ext, L"OnceBoot.Add"));
        }
        drop(L"Software\\Classes\\Directory\\Background\\shell\\OnceBoot.List");
        return had;
    }

    void UpdateContextMenu()
    {
        bool old = RemoveOldMenu();
        if (old || ContextMenuOn()) Apply(true);
    }

    bool SetContextMenu(bool on)
    {
        bool ok = Apply(on);
        if (on && !ok) Apply(false);   // half made: none at all
        return ok;
    }
}
