// ============================================================
//  BootHop - bootlist.h
//  The window of BootHop.exe started without a file: a list of the
//  .wim / .vhd / .vhdx files to boot once from. [Add...] picks files with
//  the file dialog, [Remove] (also Del) takes one off the list, [Boot
//  once] (also a double click or Enter) runs RunOneTimeBoot on the one
//  chosen. The list is kept in BootHop.list next to the exe (UTF-8, one
//  full path per line), written at each change. One list window at a
//  time (a named mutex and a window property).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    // until the window is closed; the exit code of the process. addFile
    // ("BootHop.exe --add <file>", the right-click menu): added to the list
    // and chosen. When a list window is open already (another process),
    // the file goes to that one and this process ends.
    int RunBootList(const wstring& addFile = wstring());
}
