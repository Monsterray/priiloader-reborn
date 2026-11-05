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

### Remaining defects (2026-11-04)

Checked with host tests (`tests/test_host_http.py`, `tests/test_host_priiloader.py`, also under ASan), a clean build with no warnings, and an independent read-only review. Not yet run on a Wii.

| Kind | Change | Check |
| --- | --- | --- |
| Fix | `HTTP_Parser.cpp`: a negative or huge `Content-Length` reached malloc and recv as ~4 GB; the socket leaked on every call and when the host lookup failed; redirects never worked. `HttpGet` is now a loop of at most 5 redirects; bodies are capped at 16 MB; header names match in any case. | `test_http`, 21 cases (19 fail on the old code) |
| Fix | Update check: `UpdateStruct` not initialised; an empty changelog wrote `buffer[-1]`. | build |
| Fix | A failed DOL or System Menu boot left IRQs off and hung; now restored when the loader returns. | build |
| Fix | `meta.xml` with an empty `<name/>` or `<arg/>` dropped the AHBPROT flag and later arguments. | build |
| Fix | Play log got one truncated byte of the title id (`BuildPlaylogId`). Leaks and an unchecked TMD size in `titles.cpp`; printf argument mismatches. | `test_playlog_id` |
| Fix | `LaunchWiiDisc`: disc-controlled partition count indexed a 4-entry buffer; apploader length uncapped into 0x81200000 (now at most 0x600000, the rest of MEM1); cleanup on every exit. | build |
| Fix | `DecryptvWiiSysMenu`: offsets and sizes checked against the file; SHA and AES closed on every path; patch scanners stay inside the file. | build |
| Fix | `mount.cpp`: `ShutdownMounts` joined the poll thread while holding the mutex the thread takes (deadlock before the System Menu boot); poll every 500 ms, not 0.5 ms. | build |
| Fix | `mem2_manager.c`: `strndup` read past short strings, `calloc` overflow, `memalign` ignored `align` (now refuses more than 32). | `test_mem2` (5 of 10 fail on the old code) |
| Fix | `settings.cpp`: a stats failure deleted the settings file and a short read was taken as valid; both now use defaults and leave the file alone. | `test_settings` (2 fail on the old code) |
| Fix | `PatchIOS`: stopped on the total match count, so duplicates could end the scan before ES_Identify was patched; now stops when every patch has applied at least once (never patches less than before). Result clamped to the s8 range. `DisableAHBProt` memalign argument order. | `test_iosscan` |

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

### Updater and speed (2026-11-04)

Settings and autoboot speed are in commit 50c4ba8, the TLS layer in f390a80; the updater change on top of them is pending (not committed). Checked with host tests (`tests/test_host_http.py` 37 cases, `tests/test_host_update.py` 19 cases, `tests/test_host_tls.py`), a clean build with no warnings, and a Dolphin run of `tests/dolphin/update_probe` (real HTTPS to GitHub). Not yet run on a Wii.

| Kind | Change | Check |
| --- | --- | --- |
| Feature | `HttpGetUrl` (https and http, redirects across hosts, https to http refused, chunked refused, a body without `Content-Length` over https must end with close_notify). `HttpGet` for the DacoTaco server behaves as before. | `test_http`; the 21 old cases pass on the new parser and on the old one |
| Feature | GitHub update source (`update_github.cpp`): bounded JSON reader, tag format, release choice, sha256 digest required, changelog from the release notes. `Update from:` setting, default GitHub. Convention in `docs/updates.md`. | `test_update`; `update_probe` in Dolphin: our repository (no releases, 2 byte reply), the Mbed-TLS release list (48224 bytes), a download through the github.com to release-assets.githubusercontent.com redirect (88 bytes and 5473689 bytes) matching GitHub's digest, a corrupted copy refused |
| Finding | Every GitHub reply seen in Dolphin had a `Content-Length` and ended with close_notify (api.github.com, github.com 302 with `Content-Length: 0`, release-assets.githubusercontent.com). The close-delimited path is covered by host tests only. | `update_probe` log |
| Finding | Memory: `priiloader.dol` is 1076448 bytes (836576 before TLS, +239872). The base image still starts at 0x81330000 and ends (bss) at 0x814ba760, 3430560 bytes below the end of MEM1; the wrapper and payload end at 0x8010abe0. libc's heap peaked at 725856 bytes in the probe (all TLS work, MEM2 holds the downloads). Nothing collides. | objdump, DOL headers, `update_probe` |

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

Wii checks for the remaining-defect fixes: a System Menu boot on a vWii; a boot with ES_Identify (System Menu IOS off), the Gecko log showing one fakesign and one EsIdentify hit; a retail disc launch; the update check against the real server.

Not fixed: `PatchIOSKernel` never counts its patches, so it always scans the whole range (its caller ignores the result). The changelog lines keep a leading newline for CRLF text.

Wii checks for the updater and TLS (the owner at the Wii; a real Wii differs from Dolphin on all of these):

- HTTPS on a real Wii: the clock (a Wii with the RTC wrong must say so, not fail with a certificate text), the entropy source (the jitter measurement `tls_entropy_get_stats` reports; Dolphin's timing is not the Wii's), how long connect and handshake take, heap use (the free MEM1 after bss is 3.4 MB).
- An update install from GitHub: attended, with a fresh BootMii NAND backup. It needs a release with `Priiloader_Update.dol` (none exists yet; see `docs/updates.md`). Check the digest refusal with a wrong file as well.
- Autoboot key wait timing after the speed changes (50c4ba8): the keys held at power-on must still be seen.

Size: `priiloader.dol` is now about 1.07 MB (1076448 bytes), 240 KB of it for TLS (mbedTLS and the roots), on top of the 826368 to 838304 byte figures below.

Speed (autoboot path):

- A fixed `usleep(500000)` before every autoboot decision (main.cpp ~2509).
- `state.dat` read twice (`CheckBootState`, then `GetStateFlags`).
- The hacks file is read 32 bytes per ISFS call on every System Menu boot.

Size (priiloader.dol is 838304 bytes; 826368 before these fixes):

- About 95 KB of exception tables (`.eh_frame`, `.eh_frame_hdr`); the code has 89 `try`/`catch` lines, so dropping exceptions is a refactor.
- `wii_font_Bitmap` is 97280 bytes.
- `-Os` and `-ffunction-sections` are untried.

Test layers:

- A Dolphin run with a known log line (USB Gecko output).
