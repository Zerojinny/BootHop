// ============================================================
//  BootHop - menu.h
//  The right-click menu items of .wim / .vhd / .vhdx files, whatever
//  program opens them (the current user only, no administrator rights):
//  HKCU\Software\Classes\SystemFileAssociations\<ext>\shell\BootHop
//  ("Boot once", "BootHop.exe" "%1") and ...\shell\BootHop.Add ("Add to
//  the boot list", --add "%1"); and "Show the boot list" in the menu of
//  the empty space of a folder and of the desktop
//  (...\Classes\Directory\Background\shell\BootHop.List, "BootHop.exe").
//  Windows 11 shows them under "Show more options".
//  Turned on / off in the list window (a check box) or with
//  "BootHop /menu on|off"; the keys themselves are the setting. While it
//  is on, each start of the GUI brings it up to date (the path of the
//  exe, the language).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    bool ContextMenuOn();                 // the items are there
    // after the language is loaded: the items again when they are there and
    // differ; errors are ignored (the menu is a convenience)
    void UpdateContextMenu();
    // made or removed; false = not done (the registry refused)
    bool SetContextMenu(bool on);
}
