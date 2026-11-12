// ============================================================
//  BootHop - wimg.cpp
//  wimgapi.dll of Windows (wimg.h). The declarations are those of
//  wimgapi.h of the Windows ADK, written here.
// ============================================================
#include "wimg.h"

namespace wimsmith
{
    namespace wg
    {
        // WIM_INFO (wimgapi.h)
        struct WimInfoStruct
        {
            WCHAR WimPath[MAX_PATH];
            GUID Guid;
            DWORD ImageCount;
            DWORD CompressionType;
            USHORT PartNumber;
            USHORT TotalParts;
            DWORD BootIndex;
            DWORD WimAttributes;
            DWORD WimFlagsAndAttr;
        };

        typedef HANDLE(WINAPI* CreateFileFn)(PCWSTR path, DWORD access, DWORD creation, DWORD flags, DWORD compression, PDWORD result);
        typedef BOOL(WINAPI* CloseHandleFn)(HANDLE h);
        typedef BOOL(WINAPI* SetTemporaryPathFn)(HANDLE wim, PCWSTR path);
        typedef BOOL(WINAPI* GetAttributesFn)(HANDLE wim, WimInfoStruct* info, DWORD size);
        typedef BOOL(WINAPI* GetImageInformationFn)(HANDLE h, PVOID* info, PDWORD size);
        typedef HANDLE(WINAPI* LoadImageFn)(HANDLE wim, DWORD index);
        typedef BOOL(WINAPI* SetBootImageFn)(HANDLE wim, DWORD index);
        // not in the documentation, exported by every wimgapi.dll since Windows 7
        typedef BOOL(WINAPI* ExtractImagePathFn)(HANDLE image, PCWSTR imagePath, PCWSTR destination, DWORD flags);

        static CreateFileFn pCreateFile;
        static CloseHandleFn pCloseHandle;
        static SetTemporaryPathFn pSetTemporaryPath;
        static GetAttributesFn pGetAttributes;
        static GetImageInformationFn pGetImageInformation;
        static LoadImageFn pLoadImage;
        static SetBootImageFn pSetBootImage;
        static ExtractImagePathFn pExtractImagePath;

        wstring Load()
        {
            if (pCreateFile) return wstring();
            HMODULE m = LoadLibraryExW(L"wimgapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!m) return L"wimgapi.dll: " + ErrorMessage(GetLastError());
            pCloseHandle = (CloseHandleFn)GetProcAddress(m, "WIMCloseHandle");
            pSetTemporaryPath = (SetTemporaryPathFn)GetProcAddress(m, "WIMSetTemporaryPath");
            pGetAttributes = (GetAttributesFn)GetProcAddress(m, "WIMGetAttributes");
            pGetImageInformation = (GetImageInformationFn)GetProcAddress(m, "WIMGetImageInformation");
            pLoadImage = (LoadImageFn)GetProcAddress(m, "WIMLoadImage");
            pSetBootImage = (SetBootImageFn)GetProcAddress(m, "WIMSetBootImage");
            pExtractImagePath = (ExtractImagePathFn)GetProcAddress(m, "WIMExtractImagePath");
            CreateFileFn create = (CreateFileFn)GetProcAddress(m, "WIMCreateFile");
            if (!create || !pCloseHandle || !pSetTemporaryPath || !pGetAttributes || !pGetImageInformation || !pLoadImage || !pSetBootImage || !pExtractImagePath)
                return L"wimgapi.dll of this Windows lacks a function BootHop needs.";
            pCreateFile = create;
            return wstring();
        }

        // a handle of wimgapi, closed at the end of the block
        struct Handle
        {
            HANDLE h = nullptr;
            ~Handle() { if (h) pCloseHandle(h); }
        };

        static wstring LastError(const wstring& what)
        {
            DWORD e = GetLastError();
            return what + L": " + ErrorMessage(e) + L" (" + Num((long long)e) + L")";
        }

        static wstring Open(const wstring& wimFile, bool write, const wstring& tempDir, Handle& wim)
        {
            wstring err = Load();
            if (!err.empty()) return err;
            DWORD created = 0;
            wim.h = pCreateFile(wimFile.c_str(), write ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, OPEN_EXISTING, 0, 0, &created);
            if (!wim.h) return LastError(L"Open " + wimFile);
            // wimgapi needs a folder of its own for WIMLoadImage
            if (!tempDir.empty() && !pSetTemporaryPath(wim.h, tempDir.c_str())) return LastError(L"WIMSetTemporaryPath " + tempDir);
            return wstring();
        }

        // the text of the first <tag>...</tag> in xml (entities of XML decoded)
        static wstring Tag(const wstring& xml, const wstring& tag)
        {
            size_t a = xml.find(L"<" + tag + L">");
            if (a == wstring::npos) return wstring();
            a += tag.size() + 2;
            size_t b = xml.find(L"</" + tag + L">", a);
            if (b == wstring::npos) return wstring();
            wstring v = xml.substr(a, b - a), r;
            static const wchar_t* const ents[][2] = { { L"&lt;", L"<" }, { L"&gt;", L">" }, { L"&quot;", L"\"" }, { L"&apos;", L"'" }, { L"&amp;", L"&" } };
            for (size_t i = 0; i < v.size(); i++)
            {
                bool done = false;
                if (v[i] == L'&')
                    for (const auto& e : ents)
                        if (v.compare(i, wcslen(e[0]), e[0]) == 0) { r += e[1]; i += wcslen(e[0]) - 1; done = true; break; }
                if (!done) r += v[i];
            }
            return Trim(r);
        }

        wstring ReadInfo(const wstring& wimFile, WimFileInfo& info)
        {
            info = WimFileInfo();
            Handle wim;
            wstring err = Open(wimFile, false, wstring(), wim);
            if (!err.empty()) return err;
            WimInfoStruct w = {};
            if (!pGetAttributes(wim.h, &w, sizeof w)) return LastError(L"WIMGetAttributes");
            info.ImageCount = (int)w.ImageCount;
            info.BootIndex = (int)w.BootIndex;
            info.PartNumber = w.PartNumber;
            info.TotalParts = w.TotalParts;
            info.FileBytes = FileSize(wimFile);

            // <WIM><IMAGE INDEX="1"><NAME>..</NAME><WINDOWS><INSTALLATIONTYPE>..
            PVOID p = nullptr;
            DWORD size = 0;
            if (!pGetImageInformation(wim.h, &p, &size) || !p) return LastError(L"WIMGetImageInformation");
            wstring xml((const wchar_t*)p, size / sizeof(wchar_t));
            LocalFree(p);
            for (size_t at = xml.find(L"<IMAGE "); at != wstring::npos; at = xml.find(L"<IMAGE ", at + 1))
            {
                size_t end = xml.find(L"</IMAGE>", at);
                if (end == wstring::npos) break;
                wstring one = xml.substr(at, end - at);
                WimImage im;
                size_t q = one.find(L"INDEX=\"");
                im.Index = q == wstring::npos ? 0 : (int)ParseLong(one.substr(q + 7, one.find(L'"', q + 7) - q - 7), 0);
                im.Name = Tag(one, L"NAME");
                im.InstallationType = Tag(one, L"INSTALLATIONTYPE");
                if (im.Index > 0) info.Images.push_back(im);
            }
            // an image without XML: still listed by its number
            for (int n = 1; n <= info.ImageCount; n++)
            {
                bool seen = false;
                for (const WimImage& im : info.Images) if (im.Index == n) seen = true;
                if (!seen) { WimImage im; im.Index = n; info.Images.push_back(im); }
            }
            return wstring();
        }

        wstring HasFile(const wstring& wimFile, int index, const wstring& path, const wstring& tempDir, bool* missing)
        {
            if (missing) *missing = false;
            Handle wim;
            wstring err = Open(wimFile, false, tempDir, wim);
            if (!err.empty()) return err;
            Handle image;
            image.h = pLoadImage(wim.h, (DWORD)index);
            if (!image.h) return LastError(L"WIMLoadImage " + Num(index));
            wstring dest = Combine(tempDir, FileName(path));
            DeleteFileW(dest.c_str());
            // flags 0 (WIM_FLAG_NO_DIRACL / NO_FILEACL: ERROR_INVALID_PARAMETER). An
            // unelevated process gets ERROR_PRIVILEGE_NOT_HELD for a file that is
            // there (its security cannot be set; tried), a missing one ERROR_PATH_NOT_FOUND
            BOOL ok = pExtractImagePath(image.h, path.c_str(), dest.c_str(), 0);
            DWORD e = ok ? 0 : GetLastError();
            DeleteFileW(dest.c_str());
            if (ok || e == ERROR_PRIVILEGE_NOT_HELD) return wstring();
            if (missing) *missing = e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND;
            SetLastError(e);
            return LastError(L"WIMExtractImagePath " + path);
        }

        wstring SetBootImage(const wstring& wimFile, int index, const wstring& tempDir)
        {
            {
                Handle wim;
                wstring err = Open(wimFile, true, tempDir, wim);
                if (!err.empty()) return err;
                if (!pSetBootImage(wim.h, (DWORD)index)) return LastError(L"WIMSetBootImage " + Num(index));
            }
            WimFileInfo again;
            wstring err = ReadInfo(wimFile, again);
            if (!err.empty()) return err;
            if (again.BootIndex != index) return L"The boot image of " + wimFile + L" is still " + Num(again.BootIndex) + L".";
            return wstring();
        }
    }
}
