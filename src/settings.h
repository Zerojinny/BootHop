// ============================================================
//  BootHop - settings.h
//  <exe folder>\BootHop.ini, "key = value" lines (UTF-8):
//    language   = en | ko (default: Korean on a Korean Windows)
//    theme      = dark | light (default dark)
//    delete_sdi = 1: the boot.sdi copied next to the WIM is deleted
//                 after the next restart (default 0; read only until
//                 the check box of the list window has been used)
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    wstring ExeDir();                     // the folder of this exe, without "\" at the end
    // BootHop.exe also when this is BootHop.com (the console build): for the
    // right-click menu and the Run value, which must not open a console
    wstring GuiExe();
    wstring GetSetting(const wstring& key, const wstring& fallback);

    // boot.sdi (the empty RAM disk of a WIM start) copied next to the WIM file
    // (true), or the one of Windows used where it can be (false, the default):
    // HKCU\Software\BootHop, CopySdi (DWORD); the check box of the list window
    // (the command line has /copysdi per run and does not read it)
    bool CopySdiSetting();
    bool SetCopySdiSetting(bool on);      // false = not written
    // a boot.sdi copied for a start deleted after the restart: HKCU\Software\BootHop,
    // DeleteSdi (DWORD; the check box); without that value delete_sdi of BootHop.ini
    bool DeleteSdiSetting();
    bool SetDeleteSdiSetting(bool on);
    bool FirmwareUefi();                  // false = BIOS (winload.exe instead of winload.efi)
    // the drive is encrypted with BitLocker (on, suspended, locked, en- / decrypting)
    bool BitLockerOn(wchar_t drive);
    // the boot.sdi of this Windows that the boot manager can read at the next
    // start (a local NTFS / FAT drive, not BitLocker); "" = none: not there, or Windows PE
    // (its X: is a RAM disk, gone at the next start). Used by the GUI (the
    // confirmation) and the helper (the BCD entry) alike.
    wstring SystemBootSdi();
}
