// ============================================================
//  BootHop - util.cpp
//  Text, paths, files, drives and processes (see common.h).
// ============================================================
#include "common.h"
#include <winnetwk.h>
#include <thread>

namespace wimsmith
{
    // ---------- text ----------

    wstring FromCodePage(const std::string& s, UINT cp)
    {
        if (s.empty()) return wstring();
        int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return wstring();
        wstring w((size_t)n, L'\0');
        MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string ToCodePage(const wstring& s, UINT cp)
    {
        if (s.empty()) return std::string();
        int n = WideCharToMultiByte(cp, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string a((size_t)n, '\0');
        WideCharToMultiByte(cp, 0, s.data(), (int)s.size(), &a[0], n, nullptr, nullptr);
        return a;
    }

    wstring Lower(wstring s)
    {
        if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
        return s;
    }

    wstring Upper(wstring s)
    {
        if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size());
        return s;
    }

    wchar_t UpperChar(wchar_t c) { return Upper(wstring(1, c))[0]; }

    static bool IsSpace(wchar_t c)
    {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f'
            || c == 0x00A0 || c == 0xFEFF;
    }

    wstring Trim(const wstring& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && IsSpace(s[a])) a++;
        while (b > a && IsSpace(s[b - 1])) b--;
        return s.substr(a, b - a);
    }

    wstring TrimEnd(const wstring& s)
    {
        size_t b = s.size();
        while (b > 0 && IsSpace(s[b - 1])) b--;
        return s.substr(0, b);
    }

    bool EqualsI(const wstring& a, const wstring& b)
    {
        if (a.size() != b.size()) return false;
        if (a.empty()) return true;
        return CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) == CSTR_EQUAL;
    }

    bool StartsWithI(const wstring& s, const wstring& prefix)
    {
        return s.size() >= prefix.size() && EqualsI(s.substr(0, prefix.size()), prefix);
    }

    bool EndsWithI(const wstring& s, const wstring& suffix)
    {
        return s.size() >= suffix.size() && EqualsI(s.substr(s.size() - suffix.size()), suffix);
    }

    std::vector<wstring> SplitLines(const wstring& text)
    {
        std::vector<wstring> lines;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t nl = text.find(L'\n', start);
            if (nl == wstring::npos) nl = text.size();
            wstring line = text.substr(start, nl - start);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            lines.push_back(line);
            start = nl + 1;
        }
        return lines;
    }

    // maxParts > 0: the last part keeps the rest (as String.Split(sep, count))
    std::vector<wstring> Split(const wstring& s, wchar_t sep, size_t maxParts)
    {
        std::vector<wstring> parts;
        size_t start = 0;
        while (true)
        {
            size_t p = s.find(sep, start);
            if (p == wstring::npos || (maxParts > 0 && parts.size() + 1 == maxParts))
            {
                parts.push_back(s.substr(start));
                break;
            }
            parts.push_back(s.substr(start, p - start));
            start = p + 1;
        }
        return parts;
    }

    bool TryParseLong(const wstring& s, long long& v)
    {
        wstring t = Trim(s);
        if (t.empty()) return false;
        size_t i = 0;
        bool neg = false;
        if (t[0] == L'-' || t[0] == L'+') { neg = t[0] == L'-'; i = 1; }
        if (i >= t.size()) return false;
        unsigned long long r = 0;
        for (; i < t.size(); i++)
        {
            if (t[i] < L'0' || t[i] > L'9') return false;
            unsigned long long next = r * 10 + (unsigned)(t[i] - L'0');
            if (next / 10 != r || next > 9223372036854775808ULL) return false;
            r = next;
        }
        if (!neg && r > 9223372036854775807ULL) return false;
        v = neg ? (long long)(0 - r) : (long long)r;
        return true;
    }

    long long ParseLong(const wstring& s, long long fallback)
    {
        long long v;
        return TryParseLong(s, v) ? v : fallback;
    }

    // ---------- paths ----------

    // as Path.GetFullPath of .NET Framework: characters that are not valid
    // in a path, or a ':' elsewhere than after the drive letter, are refused
    wstring FullPath(const wstring& p)
    {
        if (Trim(p).empty()) return wstring();
        for (size_t i = 0; i < p.size(); i++)
        {
            wchar_t c = p[i];
            if (c < 32 || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*') return wstring();
            if (c == L':' && i != 1) return wstring();
        }
        DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
        if (n == 0) return wstring();
        wstring r((size_t)n, L'\0');
        n = GetFullPathNameW(p.c_str(), n, &r[0], nullptr);
        r.resize(n);
        return r;
    }

    wstring Root(const wstring& p)
    {
        wstring f = FullPath(p);
        if (f.size() >= 2 && f[1] == L':') return Upper(f.substr(0, 1)) + L":\\";
        if (f.size() > 2 && f[0] == L'\\' && f[1] == L'\\')
        {
            // \\server\share
            size_t a = f.find(L'\\', 2);
            if (a == wstring::npos) return Upper(f);
            size_t b = f.find(L'\\', a + 1);
            return Upper(b == wstring::npos ? f : f.substr(0, b));
        }
        return wstring();
    }

    wstring WithSlash(const wstring& p)
    {
        size_t b = p.size();
        while (b > 0 && p[b - 1] == L'\\') b--;
        return p.substr(0, b) + L"\\";
    }

    wstring Combine(const wstring& a, const wstring& b)
    {
        if (a.empty()) return b;
        if (!b.empty() && (b[0] == L'\\' || b[0] == L'/' || (b.size() > 1 && b[1] == L':'))) return b;
        wchar_t last = a.back();
        return last == L'\\' || last == L'/' || last == L':' ? a + b : a + L"\\" + b;
    }

    static size_t LastSep(const wstring& p)
    {
        size_t s = p.find_last_of(L"\\/");
        return s;
    }

    wstring DirName(const wstring& p)
    {
        size_t s = LastSep(p);
        if (s == wstring::npos) return wstring();
        wstring d = p.substr(0, s);
        if (d.size() == 2 && d[1] == L':') d += L"\\";
        return d;
    }

    wstring FileName(const wstring& p)
    {
        size_t s = LastSep(p);
        return s == wstring::npos ? p : p.substr(s + 1);
    }

    wstring FileExt(const wstring& p)
    {
        wstring n = FileName(p);
        size_t d = n.rfind(L'.');
        return d == wstring::npos ? wstring() : n.substr(d);
    }

    wstring FileStem(const wstring& p)
    {
        wstring n = FileName(p);
        size_t d = n.rfind(L'.');
        return d == wstring::npos ? n : n.substr(0, d);
    }

    wstring WindowsDir()
    {
        wchar_t b[MAX_PATH + 1];
        UINT n = GetWindowsDirectoryW(b, MAX_PATH + 1);
        return n > 0 && n <= MAX_PATH ? wstring(b, n) : wstring();
    }

    wstring SystemDir()
    {
        wchar_t b[MAX_PATH + 1];
        UINT n = GetSystemDirectoryW(b, MAX_PATH + 1);
        return n > 0 && n <= MAX_PATH ? wstring(b, n) : wstring();
    }

    wstring Env(const wchar_t* name)
    {
        DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
        if (n == 0) return wstring();
        wstring r((size_t)n, L'\0');
        n = GetEnvironmentVariableW(name, &r[0], n);
        r.resize(n);
        return r;
    }

    // ---------- files ----------

    bool FileExists(const wstring& p)
    {
        if (p.empty()) return false;
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirExists(const wstring& p)
    {
        if (p.empty()) return false;
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    long long FileSize(const wstring& p)
    {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return -1;
        return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    }

    bool DirHasEntries(const wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Combine(dir, L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool any = false;
        do
        {
            wstring n = fd.cFileName;
            if (n != L"." && n != L"..") { any = true; break; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return any;
    }

    wstring ErrorMessage(DWORD code)
    {
        wchar_t* buf = nullptr;
        DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, (LPWSTR)&buf, 0, nullptr);
        wstring r = n > 0 && buf ? Trim(wstring(buf, n)) : L"error " + Num(code);
        if (buf) LocalFree(buf);
        return r;
    }

    void CreateDirs(const wstring& dir)
    {
        wstring full = FullPath(dir);
        if (full.empty()) throw HelperError{ L"Not a valid folder: " + dir };
        if (DirExists(full)) return;
        wstring parent = DirName(full);
        if (!parent.empty() && parent != full) CreateDirs(parent);
        if (!CreateDirectoryW(full.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            throw HelperError{ L"Create folder " + full + L": " + ErrorMessage(GetLastError()) };
    }

    void DeleteFileOrThrow(const wstring& p)
    {
        if (DeleteFileW(p.c_str())) return;
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return;
        throw HelperError{ L"Delete " + p + L": " + ErrorMessage(e) };
    }

    void MoveFileOrThrow(const wstring& from, const wstring& to)
    {
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED))
            throw HelperError{ L"Move " + from + L" -> " + to + L": " + ErrorMessage(GetLastError()) };
    }

    bool ReadBytes(const wstring& p, std::string& data)
    {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        data.clear();
        char buf[65536];
        DWORD got;
        bool ok = true;
        while (true)
        {
            if (!ReadFile(h, buf, sizeof buf, &got, nullptr)) { ok = false; break; }
            if (got == 0) break;
            data.append(buf, got);
        }
        CloseHandle(h);
        return ok;
    }

    wstring ReadTextFile(const wstring& p)
    {
        std::string d;
        if (!ReadBytes(p, d)) throw HelperError{ L"Read " + p + L": " + ErrorMessage(GetLastError()) };
        if (d.size() >= 2 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xFE)
            return wstring((const wchar_t*)(d.data() + 2), (d.size() - 2) / 2);
        if (d.size() >= 3 && (unsigned char)d[0] == 0xEF && (unsigned char)d[1] == 0xBB && (unsigned char)d[2] == 0xBF)
            d.erase(0, 3);
        // UTF-16 without a BOM (wimlib's message file on Windows): most high bytes are 0
        if (d.size() >= 4 && d.size() % 2 == 0)
        {
            size_t zeros = 0, pairs = d.size() / 2;
            for (size_t i = 1; i < d.size(); i += 2) if (d[i] == 0) zeros++;
            if (zeros * 10 >= pairs * 6) return wstring((const wchar_t*)d.data(), pairs);
        }
        if (!d.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, d.data(), (int)d.size(), nullptr, 0) == 0)
            return FromCodePage(d, CP_ACP);
        return FromUtf8(d);
    }

    static bool WriteHandle(HANDLE h, const std::string& data)
    {
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD put = 0;
        bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size());
        CloseHandle(h);
        return ok;
    }

    bool NeedsAdminToWrite(const wstring& file)
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return false; }
        return GetLastError() == ERROR_ACCESS_DENIED && !(GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_READONLY);
    }

    bool WriteBytes(const wstring& p, const std::string& data)
    {
        return WriteHandle(CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr), data);
    }

    bool AppendBytes(const wstring& p, const std::string& data)
    {
        return WriteHandle(CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr), data);
    }

    void DeleteTree(const wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Combine(dir, L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                wstring p = Combine(dir, n);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
                else
                {
                    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                    if (!DeleteFileW(p.c_str())) { FindClose(h); throw HelperError{ L"Delete " + p + L": " + ErrorMessage(GetLastError()) }; }
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!RemoveDirectoryW(dir.c_str())) throw HelperError{ L"Delete " + dir + L": " + ErrorMessage(GetLastError()) };
    }

    // ---------- drives ----------

    UINT DriveTypeOf(wchar_t letter)
    {
        wchar_t r[] = { UpperChar(letter), L':', L'\\', 0 };
        return GetDriveTypeW(r);
    }

    bool DriveExists(wchar_t letter)
    {
        wchar_t l = UpperChar(letter);
        if (l < L'A' || l > L'Z') return false;
        return (GetLogicalDrives() & (1u << (l - L'A'))) != 0;
    }

    wstring DriveFileSystem(wchar_t letter)
    {
        wchar_t r[] = { UpperChar(letter), L':', L'\\', 0 };
        wchar_t fs[MAX_PATH + 1] = { 0 };
        if (!GetVolumeInformationW(r, nullptr, 0, nullptr, nullptr, nullptr, fs, MAX_PATH + 1)) return wstring();
        return fs;
    }

    wstring SessionFreePath(const wstring& p)
    {
        if (p.size() < 2 || p[1] != L':' || !IsCharAlphaW(p[0])) return p;
        wstring drive = p.substr(0, 2), rest = p.substr(2);
        if (GetDriveTypeW((drive + L"\\").c_str()) == DRIVE_REMOTE)
        {
            wchar_t unc[1024] = {};
            DWORD n = 1024;
            if (WNetGetConnectionW(drive.c_str(), unc, &n) == NO_ERROR && unc[0])
            {
                wstring u = unc;
                while (u.size() > 2 && u.back() == L'\\') u.pop_back();
                return u + (rest.empty() ? wstring(L"\\") : rest);
            }
            return p;
        }
        // SUBST: "\??\C:\folder"
        wchar_t dev[1024] = {};
        if (QueryDosDeviceW(drive.c_str(), dev, 1024))
        {
            wstring d = dev;
            if (d.size() >= 6 && d.compare(0, 4, L"\\??\\") == 0 && d[5] == L':' && !EqualsI(d.substr(4, 2), drive))
            {
                d = d.substr(4);
                while (!d.empty() && d.back() == L'\\') d.pop_back();
                return SessionFreePath(d + (rest.empty() ? wstring(L"\\") : rest));
            }
        }
        return p;
    }

    long long DriveFreeBytes(const wstring& root)
    {
        ULARGE_INTEGER avail, total, free;
        if (!GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &free)) return -1;
        return (long long)free.QuadPart;
    }

    // ---------- processes ----------

    static void ReadAll(HANDLE h, std::string* to)
    {
        char buf[4096];
        DWORD got;
        while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) to->append(buf, got);
    }

    int RunProcess(const wstring& exe, const wstring& args, std::string& out, std::string& err)
    {
        out.clear();
        err.clear();
        SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
        HANDLE outR, outW, errR, errW;
        if (!CreatePipe(&outR, &outW, &sa, 0)) throw HelperError{ L"CreatePipe: " + ErrorMessage(GetLastError()) };
        if (!CreatePipe(&errR, &errW, &sa, 0))
        {
            CloseHandle(outR); CloseHandle(outW);
            throw HelperError{ L"CreatePipe: " + ErrorMessage(GetLastError()) };
        }
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW si = {};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nul;
        si.hStdOutput = outW;
        si.hStdError = errW;
        PROCESS_INFORMATION pi = {};
        wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
        std::vector<wchar_t> line(cmd.begin(), cmd.end());
        line.push_back(0);
        BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi);
        DWORD startError = GetLastError();
        CloseHandle(outW);
        CloseHandle(errW);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok)
        {
            CloseHandle(outR); CloseHandle(errR);
            throw HelperError{ L"Start " + exe + L": " + ErrorMessage(startError) };
        }
        // both pipes at once: a full stderr pipe must not block the process
        std::thread errThread(ReadAll, errR, &err);
        ReadAll(outR, &out);
        errThread.join();
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(outR);
        CloseHandle(errR);
        return (int)code;
    }
}
