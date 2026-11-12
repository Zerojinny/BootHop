// ============================================================
//  BootHop - helper.cpp
//  The elevated helper: BootHop.exe --helper <work folder> (helper.h).
//  It reads job.ini and makes the next start (once)
//    ramboot: from an image of a WIM file in a RAM disk: boot index,
//             boot.sdi, BCD entry and bcdedit /bootsequence (RamBoot,
//             taken from WimSmith's helper.cpp);
//    vhdboot: from the Windows in a .vhd / .vhdx (native VHD boot): the
//             file attached read-only, its Windows partition found, an
//             osloader on it in the boot sequence (VhdBoot);
//    status:  whether an entry of BootHop is in the boot sequence
//             (STATE lines; EXIT 0 = pending, 1 = not);
//    cancel:  it taken out of the boot sequence and deleted (BootState).
//  The WIM file is read and changed with wimgapi.dll of Windows (wimg.cpp).
// ============================================================
#include "helper.h"
#include "settings.h"
#include "wimg.h"
#include <virtdisk.h>
#include <winioctl.h>

namespace wimsmith
{
    static wstring statusFile;

    // ---------- job.ini ----------

    static Job ReadJob(const wstring& file)
    {
        Job job;
        for (const wstring& line : SplitLines(ReadTextFile(file)))
        {
            size_t eq = line.find(L'=');
            if (eq != wstring::npos && eq > 0) job[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
        }
        return job;
    }

    wstring Get(const Job& job, const wstring& key)
    {
        Job::const_iterator it = job.find(Lower(key));
        return it == job.end() ? wstring() : it->second;
    }

    // ---------- status.txt ----------

    static void Status(const wstring& line)
    {
        AppendBytes(statusFile, ToUtf8(line + L"\r\n"));
    }

    static void Progress(const wstring& text)
    {
        Status(L"P -1 " + text);
    }

    static int Finish(int code, const wstring& text)
    {
        Status(L"EXIT " + Num(code) + L" " + text);
        return code;
    }

    // a program of Windows; its output goes to status.txt
    static int RunTool(const wstring& exe, const wstring& args)
    {
        std::string out, err;
        int rc = RunProcess(exe, args, out, err);
        for (const wstring& l : SplitLines(FromCodePage(out + err, CP_OEMCP)))
            if (!Trim(l).empty()) Status(L"  " + TrimEnd(l));
        return rc;
    }

    // ---------- ramboot: the next start (once) from a WIM file in a RAM disk ----------

    // fixed ids of the two BCD objects: made again for each file, so there is
    // never more than one of each (not in the boot menu, only in the boot
    // sequence of the next start). The same ids as WimSmith: an entry made by
    // one program is replaced by the other.
    static const wchar_t* const RamLoaderId = L"{7a3c1e52-4f0b-4d5e-9a61-3b2d8c0f9e11}";
    static const wchar_t* const RamOptionsId = L"{7a3c1e52-4f0b-4d5e-9a61-3b2d8c0f9e12}";
    // the loader of a VHD (BootHop only)
    static const wchar_t* const VhdLoaderId = L"{7a3c1e52-4f0b-4d5e-9a61-3b2d8c0f9e13}";

    // a value of bcdedit as one argument: quoted (the path may have spaces)
    static wstring BcdArg(const wstring& v) { return L"\"" + v + L"\""; }

    static int Bcdedit(const wstring& args, bool quiet = false)
    {
        wstring exe = Combine(SystemDir(), L"bcdedit.exe");
        if (quiet)
        {
            std::string out, err;
            return RunProcess(exe, args, out, err);
        }
        Status(L"bcdedit " + args);
        int rc = RunTool(exe, args);
        if (rc != 0) Status(L"bcdedit ended with code " + Num(rc) + L".");
        return rc;
    }

    // all entries of BootHop (the RAM disk ones are WimSmith's too)
    static void DeleteRamEntries()
    {
        Bcdedit(L"/delete " + wstring(RamLoaderId) + L" /f", true);
        Bcdedit(L"/delete " + wstring(RamOptionsId) + L" /f", true);
        Bcdedit(L"/delete " + wstring(VhdLoaderId) + L" /f", true);
    }

    // wim, index (the image), set_boot = 1 (make it the boot image of the file
    // first: a RAM disk starts the boot image), name (for the BCD description),
    // copy_sdi = 1 (boot.sdi next to the WIM, not the one of Windows)
    static int RamBoot(const Job& job)
    {
        wstring wimFile = FullPath(Get(job, L"wim"));
        int index = (int)ParseLong(Get(job, L"index"), 0);
        if (!FileExists(wimFile)) return Finish(2, L"Not found: " + wimFile);
        wstring root = Root(wimFile);
        if (root.size() != 3) return Finish(87, wimFile + L" is not on a drive with a letter: the boot manager cannot read it.");
        UINT type = DriveTypeOf(root[0]);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            return Finish(87, root + L" is not a local disk: the boot manager cannot read " + wimFile + L".");
        wstring fs = DriveFileSystem(root[0]);
        if (EqualsI(fs, L"ReFS")) return Finish(87, root + L" is ReFS: the boot manager cannot read it. Put the WIM file on an NTFS or FAT32 drive.");
        bool uefi = FirmwareUefi();
        wstring winload = uefi ? L"\\Windows\\System32\\Boot\\winload.efi" : L"\\Windows\\System32\\Boot\\winload.exe";
        Status(wstring(L"Firmware: ") + (uefi ? L"UEFI" : L"BIOS"));

        {
            // wimgapi.dll of Windows (wimg.cpp); its temporary files in the work folder
            bool setBoot = Get(job, L"set_boot") == L"1";
            wstring temp = Combine(DirName(statusFile), L"wimtemp");
            CreateDirs(temp);
            Status(L"Opening " + wimFile);
            wg::WimFileInfo info;
            wstring err = wg::ReadInfo(wimFile, info);
            if (!err.empty()) return Finish(1, err);
            if (info.TotalParts > 1) return Finish(87, wimFile + L" is a part of a split WIM: a RAM disk needs the whole file (join the parts first).");
            if (index < 1 || index > info.ImageCount)
                return Finish(87, L"Image " + Num(index) + L" is not in " + wimFile + L" (" + Num(info.ImageCount) + L" images).");
            // the loader of the image (Windows PE has it)
            err = wg::HasFile(wimFile, index, winload, temp);
            if (!err.empty()) return Finish(2, L"Image " + Num(index) + L" has no " + winload + L": it cannot start from a RAM disk (" + err + L").");
            if (setBoot && info.BootIndex != index)
            {
                Status(L"Boot image of " + wimFile + L": " + Num(info.BootIndex) + L" -> " + Num(index));
                Progress(L"update");
                err = wg::SetBootImage(wimFile, index, temp);
                if (!err.empty()) return Finish(1, L"Set the boot image: " + err);
            }
            else if (info.BootIndex != index)
                return Finish(87, L"Image " + Num(index) + L" is not the boot image of " + wimFile + L" (" + Num(info.BootIndex) + L").");
        }

        // boot.sdi (the empty RAM disk): the one of Windows where the boot manager
        // can read it (copy_sdi = 0, the default; SystemBootSdi), else next to the
        // WIM file, copied there when missing (copy_sdi = 1, or Windows PE)
        Progress(L"boot");
        wstring sdi = Get(job, L"copy_sdi") == L"1" ? wstring() : SystemBootSdi();
        if (!sdi.empty()) Status(L"boot.sdi of Windows: " + sdi);
        else
        {
            sdi = Combine(DirName(wimFile), L"boot.sdi");
            if (!FileExists(sdi))
            {
                wstring from;
                for (const wchar_t* sub : { L"Boot\\DVD\\EFI\\boot.sdi", L"Boot\\DVD\\PCAT\\boot.sdi", L"Boot\\PXE\\boot.sdi" })
                    if (from.empty() && FileExists(Combine(WindowsDir(), sub))) from = Combine(WindowsDir(), sub);
                if (from.empty()) return Finish(2, L"boot.sdi was not found in " + Combine(WindowsDir(), L"Boot") + L"; put boot.sdi (e.g. from Windows PE media, \\Boot\\boot.sdi) next to " + wimFile + L".");
                Status(L"Copying " + from + L" -> " + sdi);
                if (!CopyFileW(from.c_str(), sdi.c_str(), TRUE)) return Finish((int)GetLastError(), L"boot.sdi was not copied: " + ErrorMessage(GetLastError()));
            }
        }
        wstring drive = root.substr(0, 2), sdiDrive = sdi.substr(0, 2);   // "D:"
        wstring wimPath = wimFile.substr(2), sdiPath = sdi.substr(2);       // "\folder\file.wim"
        wstring name;
        for (wchar_t c : Get(job, L"name")) if (c != L'"') name += c;
        if (name.empty()) name = FileName(wimFile);

        DeleteRamEntries();
        wstring ram = L"ramdisk=[" + drive + L"]" + wimPath + L"," + RamOptionsId;
        wstring loader = RamLoaderId, options = RamOptionsId;
        const wstring steps[] = {
            L"/create " + options + L" /d " + BcdArg(L"BootHop RAM disk") + L" /device",
            L"/set " + options + L" ramdisksdidevice " + BcdArg(L"partition=" + sdiDrive),
            L"/set " + options + L" ramdisksdipath " + BcdArg(sdiPath),
            L"/create " + loader + L" /d " + BcdArg(L"BootHop: " + name) + L" /application osloader",
            L"/set " + loader + L" device " + BcdArg(ram),
            L"/set " + loader + L" osdevice " + BcdArg(ram),
            L"/set " + loader + L" path " + winload,
            L"/set " + loader + L" systemroot \\Windows",
            L"/set " + loader + L" detecthal yes",
            L"/set " + loader + L" winpe yes",
            L"/bootsequence " + loader };
        for (const wstring& s : steps)
        {
            int rc = Bcdedit(s);
            if (rc != 0)
            {
                DeleteRamEntries();
                return Finish(rc, L"The boot entry was not made (bcdedit " + s + L").");
            }
        }
        return Finish(0, L"OK: the next start (once) is from " + wimFile + L", image " + Num(index));
    }

    // ---------- vhdboot: the next start (once) from the Windows in a VHD ----------

    // virtdisk.dll, loaded at run time (the exe still starts without it)
    struct VirtDisk
    {
        typedef DWORD(WINAPI* OpenFn)(PVIRTUAL_STORAGE_TYPE, PCWSTR, VIRTUAL_DISK_ACCESS_MASK, OPEN_VIRTUAL_DISK_FLAG, POPEN_VIRTUAL_DISK_PARAMETERS, PHANDLE);
        typedef DWORD(WINAPI* AttachFn)(HANDLE, PSECURITY_DESCRIPTOR, ATTACH_VIRTUAL_DISK_FLAG, ULONG, PATTACH_VIRTUAL_DISK_PARAMETERS, LPOVERLAPPED);
        typedef DWORD(WINAPI* DetachFn)(HANDLE, DETACH_VIRTUAL_DISK_FLAG, ULONG);
        typedef DWORD(WINAPI* PathFn)(HANDLE, PULONG, PWSTR);
        typedef DWORD(WINAPI* InfoFn)(HANDLE, PULONG, PGET_VIRTUAL_DISK_INFO, PULONG);
        OpenFn Open = nullptr;
        AttachFn Attach = nullptr;
        DetachFn Detach = nullptr;
        PathFn PhysicalPath = nullptr;
        InfoFn Info = nullptr;
        bool Load()
        {
            HMODULE m = LoadLibraryExW(L"virtdisk.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!m) return false;
            Open = (OpenFn)GetProcAddress(m, "OpenVirtualDisk");
            Attach = (AttachFn)GetProcAddress(m, "AttachVirtualDisk");
            Detach = (DetachFn)GetProcAddress(m, "DetachVirtualDisk");
            PhysicalPath = (PathFn)GetProcAddress(m, "GetVirtualDiskPhysicalPath");
            Info = (InfoFn)GetProcAddress(m, "GetVirtualDiskInformation");
            return Open && Attach && Detach && PhysicalPath && Info;
        }
    };

    // n of \\.\PhysicalDrive<n> of an attached virtual disk, -1 = not attached
    static int AttachedDisk(VirtDisk& vd, HANDLE h)
    {
        wchar_t path[MAX_PATH] = {};
        ULONG size = sizeof path;
        if (vd.PhysicalPath(h, &size, path) != ERROR_SUCCESS) return -1;
        wstring p = Lower(path);
        size_t at = p.find(L"physicaldrive");
        return at == wstring::npos ? -1 : (int)ParseLong(p.substr(at + 13), -1);
    }

    // the volumes on disk n ("\\?\Volume{...}\")
    static std::vector<wstring> VolumesOnDisk(int disk)
    {
        std::vector<wstring> list;
        wchar_t name[MAX_PATH];
        HANDLE f = FindFirstVolumeW(name, MAX_PATH);
        if (f == INVALID_HANDLE_VALUE) return list;
        do
        {
            wstring v = name;
            if (v.size() < 2) continue;
            HANDLE h = CreateFileW(v.substr(0, v.size() - 1).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            union { VOLUME_DISK_EXTENTS e; unsigned char b[512]; } ext = {};
            DWORD got = 0;
            if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &ext, sizeof ext, &got, nullptr)
                && ext.e.NumberOfDiskExtents >= 1 && (int)ext.e.Extents[0].DiskNumber == disk)
                list.push_back(v);
            CloseHandle(h);
        } while (FindNextVolumeW(f, name, MAX_PATH));
        FindVolumeClose(f);
        return list;
    }

    // "\\?\Volume{...}\" -> "\Device\HarddiskVolume<n>" ("" = unknown)
    static wstring VolumeDevice(const wstring& volume)
    {
        if (volume.size() < 6) return wstring();
        wstring name = volume.substr(4, volume.size() - 5);   // "Volume{...}"
        wchar_t target[MAX_PATH] = {};
        return QueryDosDeviceW(name.c_str(), target, MAX_PATH) ? wstring(target) : wstring();
    }

    // vhd (the file), name (for the BCD description)
    static int VhdBoot(const Job& job)
    {
        wstring vhdFile = FullPath(Get(job, L"vhd"));
        if (!FileExists(vhdFile)) return Finish(2, L"Not found: " + vhdFile);
        wstring root = Root(vhdFile);
        if (root.size() != 3) return Finish(87, vhdFile + L" is not on a drive with a letter: the boot manager cannot read it.");
        UINT type = DriveTypeOf(root[0]);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            return Finish(87, root + L" is not a local disk: the boot manager cannot read " + vhdFile + L".");
        wstring fs = DriveFileSystem(root[0]);
        if (!EqualsI(fs, L"NTFS"))
            return Finish(87, root + L" is " + (fs.empty() ? wstring(L"not ready") : fs) + L": Windows starts from a VHD only on an NTFS drive.");
        bool uefi = FirmwareUefi();
        wstring winload = uefi ? L"Windows\\System32\\Boot\\winload.efi" : L"Windows\\System32\\Boot\\winload.exe";
        wstring loaderPath = uefi ? L"\\Windows\\system32\\winload.efi" : L"\\Windows\\system32\\winload.exe";
        Status(wstring(L"Firmware: ") + (uefi ? L"UEFI" : L"BIOS"));

        VirtDisk vd;
        if (!vd.Load()) return Finish(126, L"virtdisk.dll (virtual disks of Windows) is not available here.");
        VIRTUAL_STORAGE_TYPE st = {};   // unknown: the type from the extension
        OPEN_VIRTUAL_DISK_PARAMETERS op = {};
        op.Version = OPEN_VIRTUAL_DISK_VERSION_2;
        op.Version2.ReadOnly = TRUE;
        HANDLE h = nullptr;
        Status(L"Opening " + vhdFile);
        DWORD e = vd.Open(&st, vhdFile.c_str(), VIRTUAL_DISK_ACCESS_NONE, OPEN_VIRTUAL_DISK_FLAG_NONE, &op, &h);
        if (e != ERROR_SUCCESS)
            return Finish((int)e, L"Open " + vhdFile + L": " + ErrorMessage(e)
                + (e == ERROR_SHARING_VIOLATION ? wstring(L" (attached or used by another program: detach it first)") : wstring()));
        struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer = { h };

        // a dynamic / differencing VHD grows to its full size when Windows starts from it
        GET_VIRTUAL_DISK_INFO info = {};
        ULONG infoSize = sizeof info;
        info.Version = GET_VIRTUAL_DISK_INFO_SIZE;
        if (vd.Info(h, &infoSize, &info, nullptr) == ERROR_SUCCESS)
        {
            unsigned long long virt = info.Size.VirtualSize, phys = info.Size.PhysicalSize;
            GET_VIRTUAL_DISK_INFO sub = {};
            infoSize = sizeof sub;
            sub.Version = GET_VIRTUAL_DISK_INFO_PROVIDER_SUBTYPE;
            ULONG subtype = vd.Info(h, &infoSize, &sub, nullptr) == ERROR_SUCCESS ? sub.ProviderSubtype : 0;
            Status(L"Virtual size " + Num((long long)virt) + L" bytes, file " + Num((long long)phys) + L" bytes, type " + Num(subtype) + L" (2 fixed, 3 dynamic, 4 differencing)");
            if (subtype != 2 && virt > phys)
            {
                long long need = (long long)(virt - phys), free = DriveFreeBytes(root);
                if (free >= 0 && free < need)
                    return Finish(112, L"A dynamic VHD grows to its full size (" + Num((long long)virt) + L" bytes) when Windows starts from it: " + root + L" needs "
                        + Num(need) + L" bytes free, it has " + Num(free) + L". Free some space, or convert the VHD to a fixed size.");
            }
        }

        // attached (read-only, without drive letters) to find its Windows partition
        int disk = AttachedDisk(vd, h);
        bool attachedHere = false;
        if (disk < 0)
        {
            ATTACH_VIRTUAL_DISK_PARAMETERS ap = {};
            ap.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
            Status(L"Attaching " + vhdFile + L" (read-only)");
            e = vd.Attach(h, nullptr, (ATTACH_VIRTUAL_DISK_FLAG)(ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY | ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER), 0, &ap, nullptr);
            if (e != ERROR_SUCCESS) return Finish((int)e, L"Attach " + vhdFile + L": " + ErrorMessage(e));
            attachedHere = true;
            disk = AttachedDisk(vd, h);
        }
        else Status(vhdFile + L" is attached already (disk " + Num(disk) + L")");
        struct Detacher
        {
            VirtDisk& vd;
            HANDLE h;
            bool on;
            ~Detacher() { if (on) vd.Detach(h, DETACH_VIRTUAL_DISK_FLAG_NONE, 0); }
        } detacher = { vd, h, attachedHere };
        if (disk < 0) return Finish(1, L"The disk number of " + vhdFile + L" was not found.");

        // its volumes come up some moments after the attach
        Progress(L"boot");
        wstring windows;
        std::vector<wstring> volumes;
        for (int n = 0; n < 40 && windows.empty(); n++)
        {
            if (n > 0) Sleep(500);
            volumes = VolumesOnDisk(disk);
            for (const wstring& v : volumes)
                if (windows.empty() && FileExists(v + winload)) windows = v;
        }
        if (volumes.empty()) return Finish(2, vhdFile + L" (disk " + Num(disk) + L") has no volume Windows can read.");
        if (windows.empty())
            return Finish(2, L"No volume of " + vhdFile + L" has \\" + winload + L": there is no Windows in it that starts on this PC ("
                + (uefi ? L"UEFI" : L"BIOS") + L").");
        wstring device = VolumeDevice(windows);   // only for the log
        Status(L"Windows: " + windows + (device.empty() ? wstring() : L" (" + device + L")"));

        wstring name;
        for (wchar_t c : Get(job, L"name")) if (c != L'"') name += c;
        if (name.empty()) name = FileStem(vhdFile);
        wstring loader = VhdLoaderId;
        DeleteRamEntries();
        // the device is the VHD file: "vhd=[F:]\path.vhdx" without a locate
        // element makes the boot manager look in the VHD for the partition
        // with the application path (device) / systemroot (osdevice), so a
        // VHD with several partitions (EFI, MSR, Windows) works too (bcdedit
        // /? formats; bcdboot writes the same with locate=custom:12000002).
        // "partition=\Device\HarddiskVolume<n>" of the attached VHD is stored
        // as a plain partition of a disk that is not there at boot (tried).
        wstring vhdDevice = BcdArg(L"vhd=[" + root.substr(0, 2) + L"]" + vhdFile.substr(2));
        const wstring steps[] = {
            L"/create " + loader + L" /d " + BcdArg(L"BootHop: " + name) + L" /application osloader",
            L"/set " + loader + L" device " + vhdDevice,
            L"/set " + loader + L" osdevice " + vhdDevice,
            L"/set " + loader + L" path " + loaderPath,
            L"/set " + loader + L" systemroot \\Windows" };
        for (const wstring& s : steps)
        {
            int rc = Bcdedit(s);
            if (rc != 0) { DeleteRamEntries(); return Finish(rc, L"The boot entry was not made (bcdedit " + s + L")."); }
        }
        // the settings the other loaders share (not on every PC)
        Bcdedit(L"/set " + loader + L" inherit {bootloadersettings}", true);
        Bcdedit(L"/set " + loader + L" nx OptIn", true);
        int rc = Bcdedit(L"/bootsequence " + loader);
        if (rc != 0) { DeleteRamEntries(); return Finish(rc, L"The boot entry was not made (bcdedit /bootsequence)."); }
        return Finish(0, L"OK: the next start (once) is from the Windows in " + vhdFile);
    }

    // ---------- status / cancel (the command line: BootHop /status, /cancel) ----------

    // the output of bcdedit (its element names are English in every language of Windows)
    static std::vector<wstring> BcdLines(const wstring& args, int& rc)
    {
        std::string out, err;
        rc = RunProcess(Combine(SystemDir(), L"bcdedit.exe"), args, out, err);
        return SplitLines(FromCodePage(out + err, CP_OEMCP));
    }

    // the value of an element: "name   value" (and its continuation lines, for lists)
    static std::vector<wstring> BcdValue(const std::vector<wstring>& lines, const wstring& name)
    {
        std::vector<wstring> values;
        bool in = false;
        for (const wstring& l : lines)
        {
            if (!l.empty() && l[0] != L' ' && l[0] != L'\t')
            {
                in = false;
                size_t sp = l.find_first_of(L" \t");
                if (sp != wstring::npos && EqualsI(l.substr(0, sp), name)) { in = true; values.push_back(Trim(l.substr(sp))); }
            }
            else if (in && !Trim(l).empty()) values.push_back(Trim(l));
        }
        return values;
    }

    // STATE lines for the command line: "STATE pending|entry <id> <description>";
    // pending = in the boot sequence of {bootmgr}
    static int BootState(bool cancel)
    {
        int rc = 0;
        std::vector<wstring> seq = BcdValue(BcdLines(L"/enum {bootmgr} /v", rc), L"bootsequence");
        if (rc != 0) return Finish(rc, L"bcdedit /enum {bootmgr} failed.");
        bool any = false, pending = false;
        for (const wchar_t* id : { RamLoaderId, VhdLoaderId })
        {
            int r = 0;
            std::vector<wstring> lines = BcdLines(L"/enum " + wstring(id) + L" /v", r);
            if (r != 0) continue;   // no such entry
            std::vector<wstring> d = BcdValue(lines, L"description");
            bool inSeq = false;
            for (const wstring& s : seq) if (EqualsI(s, id)) inSeq = true;
            Status(L"STATE " + wstring(inSeq ? L"pending " : L"entry ") + id + L" " + (d.empty() ? wstring() : d[0]));
            any = true;
            if (inSeq) pending = true;
            if (cancel && inSeq) Bcdedit(L"/bootsequence " + wstring(id) + L" /remove", true);
        }
        if (cancel)
        {
            DeleteRamEntries();
            return Finish(0, any ? L"The entries of BootHop were deleted." : L"There was no entry of BootHop.");
        }
        return Finish(pending ? 0 : 1, pending ? L"The next start is from an entry of BootHop." : L"No start from BootHop is pending.");
    }

    // ---------- entry ----------

    int RunHelper(const std::vector<wstring>& args)
    {
        if (args.size() < 2) return 87;
        wstring work = args[1];
        statusFile = Combine(work, L"status.txt");
        if (!WriteBytes(statusFile, std::string())) return 5;
        try
        {
            Job job = ReadJob(Combine(work, L"job.ini"));
            wstring mode = Get(job, L"mode");
            if (mode == L"vhdboot") return VhdBoot(job);
            if (mode == L"status" || mode == L"cancel") return BootState(mode == L"cancel");
            if (mode != L"ramboot") return Finish(87, L"Unknown mode: " + mode);
            return RamBoot(job);
        }
        catch (const HelperError& e) { return Finish(1, e.msg); }
        catch (const std::exception& e) { return Finish(1, FromUtf8(e.what())); }
    }
}
