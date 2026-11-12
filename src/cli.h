// ============================================================
//  BootHop - cli.h
//  The command line: BootHop /boot <file> [/index N] [/restart] [/copysdi],
//  BootHop /status, BootHop /cancel, BootHop /? (cli.cpp).
//  BootHop.com is used on its own, independent of BootHop.exe: its
//  options are given per run, it reads no setting the window saves
//  (the right-click menu, "copy boot.sdi") and has no commands for them.
//  Without questions or windows (only the UAC prompt of the helper);
//  the texts go to the console. BootHop.com (the same program built for
//  the console, build.bat) is what a command prompt starts for
//  "BootHop": it waits and gives the exit code; BootHop.exe prints to
//  the console it was started from but the prompt does not wait for it.
//  BootHop.com works without BootHop.exe too (the helper through the
//  "runas" of the class exefile, ramboot.cpp RunElevated).
//  Exit codes: 0 = done (/status: a start from BootHop is pending),
//  1 = failed (/status: none pending), 2 = wrong arguments,
//  1223 = the UAC prompt was cancelled, else the code of the helper.
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    // true when args[0] is a command ("/..."); then rc is the exit code
    bool RunCli(const std::vector<wstring>& args, int& rc);
    // a line (or lines) on the console: stdout, or stderr for an error
    void Print(const wstring& text, bool error);
}
