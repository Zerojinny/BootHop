// ============================================================
//  BootHop - wimg.h
//  WIM files through wimgapi.dll of Windows (System32; also in Windows
//  PE), the library DISM itself uses: loaded at run time, so no header
//  or import library of the ADK is needed (wimg.cpp). Only what a boot
//  from a RAM disk needs: the images and the boot image of a file,
//  whether an image has a file (the loader), and setting the boot image.
//  Every function returns "" when done, else the error text.
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    namespace wg
    {
        struct WimImage
        {
            int Index = 0;
            wstring Name, InstallationType;   // INSTALLATIONTYPE of the XML ("WindowsPE", "Client", ...)
        };

        struct WimFileInfo
        {
            int ImageCount = 0, BootIndex = 0, PartNumber = 0, TotalParts = 0;
            long long FileBytes = -1;
            std::vector<WimImage> Images;
        };

        wstring Load();   // System32\wimgapi.dll and its functions
        // the header and the XML of the images (opened for reading only)
        wstring ReadInfo(const wstring& wimFile, WimFileInfo& info);
        // the file path (from the root of the image, "\Windows\...") in image
        // index: "" = it is there (extracted into tempDir and deleted again);
        // missing: set when the image surely has no such file (not another error)
        wstring HasFile(const wstring& wimFile, int index, const wstring& path, const wstring& tempDir, bool* missing = nullptr);
        // the boot image of the file (opened for writing; checked by reading it again)
        wstring SetBootImage(const wstring& wimFile, int index, const wstring& tempDir);
    }
}
