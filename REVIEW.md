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

### Setup: build and tools

| Kind | Change | Check |
| --- | --- | --- |
| Tools | wii-homebrew-kit adopted: LF line endings, agent and contributor docs, Dolphin and bench helpers | `python -m unittest discover -s tests` |
| Fix | OpenDolBoot built only with a compiler that pulled in `<iterator>` by chance, included `unistd.h` everywhere, and printed `size_t` with `%lX`; the bin2s symbols had no C linkage, so an MSVC-ABI compiler could not link either tool | builds with msys g++ and clang++; outputs byte-identical |
| Fix | OpenDolBoot's Windows branch tested `uname` against `WIN32`, which no shell prints | Git Bash build picks the Windows branch |

### Next

Found in a read-only review on 2026-11-04 (source traced, not yet run). Fix the first group before anything else, each with a host test.

Boot and NAND safety:

- `installer.cpp` (~386): when writing Priiloader fails, the restore of the System Menu copy runs in `try{}catch(...){}` and the copy is deleted anyway. On an update the TMD already holds Priiloader's hash. Delete the copy only after a verified restore.
- `installer.cpp` `PatchTMD` error path: a failed TMD restore is swallowed and the backup deleted.
- `nand.cpp` `NandWrite`: a size mismatch throws with `ret` still 0, so the caller sees success and patches the TMD for a file that was not written. `fileDescriptor` starts at 0 and is closed on every path.
- `main.cpp` `AutoBootDol` (~1808): reads the whole of `main.nfo` into a `STACK_ALIGN(_dol_settings)` buffer (10-byte struct); arguments longer than the alignment slack overwrite the stack at every autoboot. `arg_cli_length` is not checked against the file.
- `hacks.cpp` `LoadSystemHacks`: early returns leave `states_hash` shorter than `system_hacks`; `BootMainSysMenu` indexes it anyway.
- `password.cpp` `password_check` (284): `cpstatus` is used after `mem_free` set it to NULL. `check_pass` and `password_check` write one byte past VLAs; `InstallPassword` writes `pbuf[psize]` out of bounds. The Y+X backdoor skips the password.
- `dvd.c` (230, 276): `if (!di_fd)` should be `di_fd < 0`; a failed async call leaves `async_called` set, so `DVDAsyncBusy()` loops for ever at shutdown and channel launch.
- `loader.c` (228): ELF entry check joins the range tests with `&&`, so it never fires.

Other defects:

- `HTTP_Parser.cpp`: negative `Content-Length` becomes a 4 GB length; the socket leaks on every call; redirects never match (`location: ` prefix kept).
- `main.cpp` update check: `UpdateStruct` not initialised; `file_size == 0` writes `buffer[-1]`.
- `titles.cpp` (626): play log gets one truncated byte of the title id. Leaks at 475 and 511.
- `mount.cpp` `ShutdownMounts`: joins the poll thread while holding the mutex the thread takes.
- `IOS.cpp` `PatchIOS`: the stop condition counts matches, not patches, so it often scans the whole range.
- `BootDolFromFile`/`InstallLoadDOL`: argument buffer sized with `strnlen(arg,128)` but filled with `strcpy`; `fclose` twice.

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
