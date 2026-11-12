// ============================================================
//  BootHop - helper.h
//  The elevated helper: BootHop.exe --helper <work folder> (helper.cpp).
//  The contract of the WimSmith helper, with one job only (mode ramboot):
//    <work>\job.ini      the job ("key = value", UTF-8)
//    <work>\status.txt   progress "P <percent> <phase> ...", log lines,
//                        at the end "EXIT <code> <text>"
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    int RunHelper(const std::vector<wstring>& args);

    // ---- job.ini ----
    wstring Get(const Job& job, const wstring& key);
}
