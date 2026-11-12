// ============================================================
//  BootHop - common.h
//  Text, paths, files and processes on plain Win32 (util.cpp). All
//  text is UTF-16 (std::wstring); files are read and written as UTF-8.
// ============================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wimsmith
{
    using std::wstring;

    // job.ini: "key = value"; keys in lower case
    typedef std::map<wstring, wstring> Job;

    // a failure of a file or process operation (the helper ends with EXIT 1 <msg>)
    struct HelperError
    {
        wstring msg;
    };

    // ---- text ----
    wstring FromCodePage(const std::string& s, UINT cp);
    std::string ToCodePage(const wstring& s, UINT cp);
    inline wstring FromUtf8(const std::string& s) { return FromCodePage(s, CP_UTF8); }
    inline std::string ToUtf8(const wstring& s) { return ToCodePage(s, CP_UTF8); }
    wstring Lower(wstring s);
    wstring Upper(wstring s);
    wchar_t UpperChar(wchar_t c);
    wstring Trim(const wstring& s);
    wstring TrimEnd(const wstring& s);
    bool EqualsI(const wstring& a, const wstring& b);
    bool StartsWithI(const wstring& s, const wstring& prefix);
    bool EndsWithI(const wstring& s, const wstring& suffix);
    std::vector<wstring> SplitLines(const wstring& text);
    std::vector<wstring> Split(const wstring& s, wchar_t sep, size_t maxParts = 0);
    bool TryParseLong(const wstring& s, long long& v);
    long long ParseLong(const wstring& s, long long fallback);
    inline wstring Num(long long v) { return std::to_wstring(v); }

    // ---- paths ----
    wstring FullPath(const wstring& p);       // "" when the path is not valid
    wstring Root(const wstring& p);           // "C:\" (upper case), "" when none
    wstring WithSlash(const wstring& p);
    wstring Combine(const wstring& a, const wstring& b);
    wstring DirName(const wstring& p);
    wstring FileName(const wstring& p);
    wstring FileExt(const wstring& p);        // ".wim" ("" when none)
    wstring FileStem(const wstring& p);
    wstring WindowsDir();
    wstring SystemDir();
    wstring Env(const wchar_t* name);

    // ---- files ----
    bool FileExists(const wstring& p);
    bool DirExists(const wstring& p);
    long long FileSize(const wstring& p);     // -1 when unknown
    bool DirHasEntries(const wstring& dir);
    void CreateDirs(const wstring& dir);      // throws HelperError
    void DeleteFileOrThrow(const wstring& p); // a missing file is no error
    void MoveFileOrThrow(const wstring& from, const wstring& to);
    void DeleteTree(const wstring& dir);      // a folder and all in it; throws HelperError
    bool ReadBytes(const wstring& p, std::string& data);
    wstring ReadTextFile(const wstring& p);   // UTF-8 / UTF-16 (BOM); throws HelperError
    bool WriteBytes(const wstring& p, const std::string& data);
    // the file cannot be opened for writing for want of rights (a WIM in the
    // root of C:, in Program Files, ...): its changes need the elevated helper.
    // A file another program holds (sharing violation) is not such a case.
    bool NeedsAdminToWrite(const wstring& file);
    bool AppendBytes(const wstring& p, const std::string& data);
    wstring ErrorMessage(DWORD code);

    // ---- drives ----
    UINT DriveTypeOf(wchar_t letter);
    bool DriveExists(wchar_t letter);
    wstring DriveFileSystem(wchar_t letter);  // "" = not ready
    long long DriveFreeBytes(const wstring& root);
    // a path on a mapped network drive (\\server\share\...) or a SUBST drive
    // (its real folder) as a process of another logon session sees it: an
    // elevated process does not see the drive letters of the normal session
    wstring SessionFreePath(const wstring& p);

    // ---- processes ----
    // runs exe (no window), returns its exit code; stdout and stderr as bytes
    int RunProcess(const wstring& exe, const wstring& args, std::string& out, std::string& err);
}
