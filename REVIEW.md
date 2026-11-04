# priiloader-reborn review

## Plan

Each finding records its trigger, its effect, the smallest safe fix, and the check that shows it (a test where one can be written).

| Subsystem | What to trace | Check |
| --- | --- | --- |
| Installer (`src/Installer`) | backup copy, boot content write, TMD patch, each failure path | host test over a simulated NAND with injected failures |
| Autoboot (`main.cpp`, `state.cpp`, `settings.cpp`) | `state.dat`, settings, `main.bin`/`main.nfo` to the launched DOL | host tests on the parsers; Dolphin boot |
| System Menu hacks (`hacks.cpp`, `SystemMenu.cpp`) | `hacks_hash.ini`, `hacksh_s.ini` to the patched menu | host test on short and odd files |
| Password (`password.cpp`) | `password.txt` to the check | host test |
| Update (`HTTP_Parser.cpp`, `main.cpp`) | HTTP headers and body to the installed update | host test with canned responses |
| Titles, disc (`titles.cpp`, `dvd.c`, `DiscContent.cpp`) | TMD, banner, disc header to the launch | host test where the parser can be split out |
| Host tools (`tools/OpenDolBoot`, `tools/bootloader`) | DOL in, DOL/app out | byte compare of outputs across compilers |

## Findings

### Boot and NAND safety (2026-11-04)

Checked with host tests (`tests/test_host_installer.py`, `tests/test_host_priiloader.py`), a clean build with no warnings, and an independent read-only review of each half. Not yet run on a Wii.

| Kind | Change | Check |
| --- | --- | --- |
| Fix | Installer: a failed Priiloader write deleted the System Menu copy after a restore whose errors were swallowed; on an Update that copy was the only System Menu. The copy is now kept on Update, and on Install deleted only after a verified restore. Same for PatchTMD's TMD backup. | `*_write_fails_keeps_copy`, `patchtmd_failed_restore_keeps_backup` |
| Fix | `PriiloaderInstalled()` read any ISFS error as "not installed", so one transient error turned an Update into an Install that overwrote the System Menu copy with Priiloader. Only -106 means "not installed" now. | `installed_check_fails_loudly` |
| Fix | `NandWrite`/`NandCopy`: a size mismatch or short read/write returned success; descriptors started at 0 and were closed twice. The replace step now renames the verified temp file straight over the destination (IOS replaces an existing file, as Dolphin's IOS FS does) and falls back to upstream's delete-then-rename. | `nand_*` tests, descriptor sweep; sim follows Dolphin's rename rules (same file name only; replace), both replace modes |
| Fix | vWii: stale `fileId` in the nandloader lookup; Remove now puts back BC-NAND (not Priiloader) when it has to recreate the boot content; Remove deletes the copies only after the TMD restore succeeds. | `vwii_nandloader_without_boot_content`, `remove_*` |
| Fix | Fault sweeps: with one failure at every ISFS call of install, update, vWii install and vWii update, the NAND ends bootable; with NAND broken from call N on, it ends bootable or with the copy and TMD backup intact. | 8 sweeps |
| Fix | `AutoBootDol` read all of `main.nfo` into a 10-byte stack struct and trusted its length field. The header is parsed by `NfoParseHeader` (`nfo.cpp`); arguments go on the heap. A damaged `main.nfo` is ignored and the binary starts without arguments, as upstream did. Installing a new binary removes the old `main.nfo`. | `test_nfo` |
| Fix | `BootDolFromFile`/`InstallLoadDOL`: argument buffer sized with `strnlen(arg,128)` but filled with `strcpy`; double `fclose`. Arguments are cut at 128 characters; empty arguments are not counted. | `test_nfo` |
| Fix | `LoadSystemHacks`: early returns left `states_hash` shorter than `system_hacks`, which `BootMainSysMenu` indexes; double close, uninitialised stats. | `test_hacks` (fails on the old code) |
| Fix | `password.cpp`: use after free in `password_check`, off-by-one VLAs, `pbuf[psize]` out of bounds, leaks. | `test_password` |
| Fix | `dvd.c`: `!di_fd` for `di_fd < 0`; failed async calls left `async_called` set, so the waits spun for ever. Waits now time out after 15 s (`DVDWaitAsync`). | build; no host test |
| Fix | `loader.c`: the ELF entry check used `&&` and never fired; a returning DOL fell into the System Menu patch code with its argv as patches, now it stops. | build; `loader.bin` 2008 -> 2048 bytes |
| Fix | `state.cpp`: NULL checks on `mem_align`. | build |

Owner decisions still open: the Y+X×4 password backdoor is kept (now memory-safe); argument blocks over 8192 bytes are refused.

### Setup: build and tools

| Kind | Change | Check |
| --- | --- | --- |
| Tools | wii-homebrew-kit adopted: LF line endings, agent and contributor docs, Dolphin and bench helpers | `python -m unittest discover -s tests` |
| Fix | OpenDolBoot built only with a compiler that pulled in `<iterator>` by chance, included `unistd.h` everywhere, and printed `size_t` with `%lX`; the bin2s symbols had no C linkage, so an MSVC-ABI compiler could not link either tool | builds with msys g++ and clang++; outputs byte-identical |
| Fix | OpenDolBoot's Windows branch tested `uname` against `WIN32`, which no shell prints | Git Bash build picks the Windows branch |

### Next

From the review on 2026-11-04 (source traced, not yet run).

Wii checks for the boot and NAND safety fixes (only with the owner at the Wii and a fresh BootMii NAND backup): install, update and remove on a Wii and a vWii; autoboot of a DOL with and without `main.nfo`; whether IOS replaces an existing file on rename (Dolphin does; the code falls back if not); the vWii System Menu's real boot index (offset 0x1E0 of its TMD).

Not fixed: the boot content and the TMD are replaced by two separate renames, so power loss between them leaves a hash mismatch; the System Menu copy and `title_or.tmd` are the recovery. A fix needs a redesign.

Other defects:

- `HTTP_Parser.cpp`: negative `Content-Length` becomes a 4 GB length; the socket leaks on every call; redirects never match (`location: ` prefix kept).
- `main.cpp` update check: `UpdateStruct` not initialised; `file_size == 0` writes `buffer[-1]`.
- `titles.cpp` (626): play log gets one truncated byte of the title id. Leaks at 475 and 511.
- `mount.cpp` `ShutdownMounts`: joins the poll thread while holding the mutex the thread takes.
- `IOS.cpp` `PatchIOS`: the stop condition counts matches, not patches, so it often scans the whole range.

Speed (autoboot path):

- A fixed `usleep(500000)` before every autoboot decision (main.cpp ~2509).
- `state.dat` read twice (`CheckBootState`, then `GetStateFlags`).
- The hacks file is read 32 bytes per ISFS call on every System Menu boot.

Size (priiloader.dol is 826368 bytes):

- About 95 KB of exception tables (`.eh_frame`, `.eh_frame_hdr`); the code has 89 `try`/`catch` lines, so dropping exceptions is a refactor.
- `wii_font_Bitmap` is 97280 bytes.
- `-Os` and `-ffunction-sections` are untried.

Test layers:

- Host test harness with an ISFS stand-in, for the parsers and the installer sequence.
- A Dolphin run with a known log line (USB Gecko output).
