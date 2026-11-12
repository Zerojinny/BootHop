// ============================================================
//  BootHop - settings.cpp
//  BootHop.ini (see settings.h). Read only: the file is written by hand.
// ============================================================
#include "settings.h"
#include <shobjidl.h>
#include <propsys.h>
#include <propvarutil.h>

namespace wimsmith
{
    wstring ExeDir()
    {
        wchar_t b[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
        return DirName(wstring(b, n));
    }

    wstring GuiExe()
    {
        wchar_t b[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
        wstring self(b, n);
        if (EqualsI(FileExt(self), L".com"))
        {
            wstring exe = self.substr(0, self.size() - 4) + L".exe";
            if (FileExists(exe)) return exe;
        }
        return self;
    }

    // the settings BootHop changes itself (BootHop.ini is only read)
    static const wchar_t* const RegKey = L"Software\\BootHop";

    bool CopySdiSetting()
    {
        DWORD v = 0, size = sizeof v;
        if (RegGetValueW(HKEY_CURRENT_USER, RegKey, L"CopySdi", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return false;
        return v == 1;
    }

    bool SetCopySdiSetting(bool on)
    {
        DWORD v = on ? 1 : 0;
        return RegSetKeyValueW(HKEY_CURRENT_USER, RegKey, L"CopySdi", REG_DWORD, &v, sizeof v) == ERROR_SUCCESS;
    }

    bool DeleteSdiSetting()
    {
        DWORD v = 0, size = sizeof v;
        if (RegGetValueW(HKEY_CURRENT_USER, RegKey, L"DeleteSdi", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS) return v == 1;
        return GetSetting(L"delete_sdi", L"0") == L"1";   // before the check box: BootHop.ini
    }

    bool SetDeleteSdiSetting(bool on)
    {
        DWORD v = on ? 1 : 0;
        return RegSetKeyValueW(HKEY_CURRENT_USER, RegKey, L"DeleteSdi", REG_DWORD, &v, sizeof v) == ERROR_SUCCESS;
    }

    bool FirmwareUefi()
    {
        typedef BOOL(WINAPI* GetFirmwareTypeFn)(DWORD*);
        GetFirmwareTypeFn fn = (GetFirmwareTypeFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFirmwareType");
        DWORD type = 0;
        if (fn && fn(&type)) return type == 2;   // FirmwareTypeUefi
        // older Windows: BIOS has no firmware variables at all
        GetFirmwareEnvironmentVariableW(L"", L"{00000000-0000-0000-0000-000000000000}", nullptr, 0);
        return GetLastError() != ERROR_INVALID_FUNCTION;
    }

    // System.Volume.BitLockerProtection of the drive (the shell reads it without
    // elevation): 1 on, 2 off, 3 encrypting, 4 decrypting, 5 suspended, 6 locked;
    // 0 or none: a drive BitLocker cannot encrypt (seen 2026-11-12: 2 on
    // unencrypted NTFS drives, 0 on network drives)
    bool BitLockerOn(wchar_t drive)
    {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        int state = 0;
        PROPERTYKEY key;
        IShellItem2* item = nullptr;
        wstring root = wstring(1, drive) + L":\\";
        if (SUCCEEDED(PSGetPropertyKeyFromName(L"System.Volume.BitLockerProtection", &key))
            && SUCCEEDED(SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&item))))
        {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(item->GetProperty(key, &v)))
            {
                LONG n = 0;
                if (SUCCEEDED(PropVariantToInt32(v, &n))) state = (int)n;
                PropVariantClear(&v);
            }
            item->Release();
        }
        if (SUCCEEDED(init)) CoUninitialize();
        return state == 1 || state >= 3;
    }

    wstring SystemBootSdi()
    {
        // Windows PE (MiniNT): its Windows folder is on the RAM disk X:
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &k) == ERROR_SUCCESS)
        {
            RegCloseKey(k);
            return wstring();
        }
        wstring win = WindowsDir(), root = Root(win);
        if (root.size() != 3 || DriveTypeOf(root[0]) != DRIVE_FIXED) return wstring();
        wstring fs = DriveFileSystem(root[0]);
        if (!EqualsI(fs, L"NTFS") && !EqualsI(fs, L"FAT32") && !EqualsI(fs, L"FAT")) return wstring();
        // the boot manager may not read an encrypted drive: then copied next to the WIM
        if (BitLockerOn(root[0])) return wstring();
        for (const wchar_t* sub : { L"Boot\\DVD\\EFI\\boot.sdi", L"Boot\\DVD\\PCAT\\boot.sdi", L"Boot\\PXE\\boot.sdi" })
        {
            wstring f = Combine(win, sub);
            if (FileExists(f)) return f;
        }
        return wstring();
    }

    wstring GetSetting(const wstring& key, const wstring& fallback)
    {
        wstring f = Combine(ExeDir(), L"BootHop.ini");
        if (!FileExists(f)) return fallback;
        std::vector<wstring> lines;
        try { lines = SplitLines(ReadTextFile(f)); }
        catch (const HelperError&) { return fallback; }
        for (const wstring& l : lines)
        {
            wstring t = Trim(l);
            if (t.empty() || t[0] == L'#' || t[0] == L';') continue;
            size_t eq = t.find(L'=');
            if (eq != wstring::npos && eq > 0 && EqualsI(Trim(t.substr(0, eq)), key))
            {
                wstring v = Trim(t.substr(eq + 1));
                return v.empty() ? fallback : v;
            }
        }
        return fallback;
    }
}
