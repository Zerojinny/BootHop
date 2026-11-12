// ============================================================
//  BootHop - ramboot.h
//  Boot once from a WIM file (taken from WimSmith's "Boot once from this
//  WIM"): it reads the file (wimgapi.dll of Windows, wimg.cpp), lets the user
//  choose an image, confirms, runs the elevated helper (helper.cpp
//  RamBoot: boot index, boot.sdi, BCD entry, bcdedit /bootsequence)
//  with a small window while it works, and asks to restart. With
//  "delete boot.sdi" on (the list window) the boot.sdi it copied is deleted at
//  the first logon after the PC has started again (a Run value of the
//  user: BootHop.exe --cleanup).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    // the exit code of the process: 0 = done (or cancelled by the user)
    int RunOneTimeBoot(const wstring& wim);

    // the command line (cli.cpp): the same without a window or a question,
    // the texts on the console. index: the image of a WIM (0 = the only one,
    // else the boot image); restart: restart at once when it is done
    // copySdi: boot.sdi copied next to the WIM (/copysdi), else the one of
    // Windows; deleteSdi: a copied one deleted after the restart (/deletesdi).
    // Neither the setting the window saves (CopySdiSetting) nor BootHop.ini
    // (delete_sdi) is read here
    struct BootOptions { int index = 0; bool restart = false; bool copySdi = false; bool deleteSdi = false; };
    int RunOneTimeBootQuiet(const wstring& file, const BootOptions& o);
    // /status (0 = a start from BootHop is pending, 1 = not) and /cancel
    // (its entries taken out of the boot sequence and deleted): the helper elevated.
    // window: the cancel of the list window (a working window and a message box
    // instead of the console)
    int RunBootState(bool cancel, bool window = false);
    // the file of the start made last (window or command line) while it is still
    // pending: "" after a restart, a cancel, or a /status that found none.
    // Only the list window shows it; the command line asks the BCD (/status)
    wstring PendingBoot();
    // "BootHop.exe --cleanup <sdi> <boot time>" (Run value): deletes the
    // boot.sdi once the PC has restarted since, and the Run value with it
    int RunSdiCleanup(const wstring& sdi, const wstring& bootTime);
    // the check box of the list window (DeleteSdiSetting): delete the boot.sdi copied for it after the next start
    bool DeleteSdiAfterBoot();
    // shutdown.exe /r or /s now (no /f); "" = started, else the error
    wstring RunShutdown(bool restart);
}
