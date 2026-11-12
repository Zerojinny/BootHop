# BootHop

**English** | [한국어](README.ko.md)

BootHop is a Windows tool that starts this PC **once** from another system. The start after that goes back to your usual Windows.

- **WIM files** (such as the `boot.wim` of Windows PE): the file is loaded into a RAM disk and started from there.
- **VHD / VHDX files**: the Windows installed in the file is started directly (Windows' native VHD boot).

You don't need to make a USB drive or edit the boot menu: choose a file and restart. BootHop uses the "next start only" feature of the Windows Boot Manager (`bcdedit /bootsequence`), so your usual boot settings stay as they are.

## Requirements

- Windows 8 or later, x64
- Administrator rights (a UAC prompt appears when the boot entry is made)
- UEFI and BIOS are both supported
- The file to start from must be on a **local disk**:
  - WIM: NTFS or FAT32
  - VHD / VHDX: NTFS
  - Avoid drives encrypted with BitLocker: the Boot Manager may not be able to read the file there. BootHop warns you first when the drive is encrypted.
- A WIM boot loads the whole file into memory, so the PC needs at least 1 GB more memory than the size of the WIM.

## Usage

### The list window

Run `BootHop.exe` to open the boot list window.

1. Add `.wim` / `.vhd` / `.vhdx` files with **[Add...]**, or drag them onto the window from Explorer.
2. Choose one and press **[Boot once]** (a double click or Enter works too).
3. If the WIM has several images, choose one, then press **Yes** in the confirmation.
4. When the boot entry has been made, BootHop asks whether to restart now.

When a start is pending, it is shown at the top of the window. **[Cancel it]** cancels it.

The options at the bottom of the window:

| Option | What it does |
|---|---|
| Right-click menu | Adds BootHop items to the Explorer menu: "Boot once from this file" and "Add to the boot list" on files, "Show the boot list" on the empty space of a folder or the desktop. In Windows 11 they are under "Show more options". |
| Copy boot.sdi next to the WIM file | Off (default): the boot.sdi of Windows is used. On: it is copied next to the WIM file and used from there. |
| Delete boot.sdi after the restart | Deletes a copied boot.sdi at the first logon after the restart. |

The list is saved in `BootHop.list` next to the exe.

### Command line

`BootHop.com` is for the command prompt. It works on its own, without `BootHop.exe`, and does not read the settings of the window.

```
BootHop /boot <file> [/index N] [/restart] [/copysdi [/deletesdi]]
BootHop /status
BootHop /cancel
BootHop /?
```

| Command / option | What it does |
|---|---|
| `/boot <file>` | Sets up the next start without asking (the UAC prompt still appears). |
| `/index N` | The image number in the WIM. Without it: the only image, or else the boot image. |
| `/restart` | Restarts right after the setup. |
| `/copysdi` | Copies boot.sdi next to the WIM and uses it from there. |
| `/deletesdi` | With `/copysdi`: deletes the copied boot.sdi after the restart. |
| `/status` | Shows whether the next start is set to BootHop. |
| `/cancel` | Cancels the pending start and deletes the BootHop entry. |

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Done (`/status`: a start is pending) |
| 1 | Failed (`/status`: none pending) |
| 2 | Wrong arguments |
| 1223 | UAC prompt cancelled |

The language of the messages follows the console code page (949: Korean, otherwise English).

## Settings file (optional)

Create `BootHop.ini` next to the exe to set the language and theme of the window:

```
; language: en | ko, theme: dark | light
language = en
theme = dark
```

## Good to know

- **Only Windows PE images start from a WIM.** A regular Windows image such as `install.wim` does not start from a RAM disk.
- **The boot image of the WIM may change.** The RAM disk starts the boot image of the WIM. If you choose another image, BootHop makes it the boot image; the confirmation tells you so beforehand.
- **The VHD must not be attached when the PC starts.** A dynamically expanding VHD grows to its full size while it runs, so the drive needs that much free space.
- **Only one entry is added to the Boot Manager.** It is named "BootHop: …" and does not appear in the boot menu. A new setup or `/cancel` deletes the previous one.

## Build

Requires Visual Studio 2017 or later (Desktop development with C++). Run the command below to make `BootHop.exe` and `BootHop.com`. No other libraries are needed: only `wimgapi.dll` and `virtdisk.dll`, which come with Windows.

```
build.bat
```

## License

MIT. See `LICENSE`.
