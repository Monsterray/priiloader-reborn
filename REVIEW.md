# priiloader-reborn review

## Plan

Each finding records its trigger, its effect, the smallest safe fix, and the check that shows it (a test where one can be written).

| Subsystem | What to trace | Check |
| --- | --- | --- |
| Installer (`src/Installer`) | backup copy, boot content write, TMD patch, each failure path | host test over a simulated NAND with injected failures |
| Autoboot (`main.cpp`, `state.cpp`, `settings.cpp`) | `state.dat`, settings, `main.bin`/`main.nfo` to the launched DOL | host tests on the parsers; Dolphin boot |
| System Menu hacks (`hacks.cpp`, `SystemMenu.cpp`) | `hacks_hash.ini`, `hackstat.ini` (was `hacksh_s.ini`) to the patched menu | host test on short and odd files |
| Password (`password.cpp`) | `password.txt` to the check | host test |
| Update (`HTTP_Parser.cpp`, `main.cpp`) | HTTP headers and body to the installed update | host test with canned responses |
| Titles, disc (`titles.cpp`, `dvd.c`, `DiscContent.cpp`) | TMD, banner, disc header to the launch | host test where the parser can be split out |
| Host tools (`tools/OpenDolBoot`, `tools/bootloader`) | DOL in, DOL/app out | byte compare of outputs across compilers |

## Findings

### Hack states by name (2026-11-05)

- Which hacks are on was kept in `hacksh_s.ini`, one byte per hack by position. A new `hacks_hash.ini` with the same count in another order turned on hacks nobody chose; a different count turned all of them off. They are now kept in `hackstat.ini`, the names of the hacks that are on, one per line (hacks of the same name are versions of one hack; none of them overlap in System Menu version). An old file whose size matches the hack list is moved over once and deleted; a new save deletes it too, so an older Priiloader never reads stale positions (it then starts with every hack off). Settings (`loader.ini`) stay a fixed-size binary record: it is read on every boot before video, and a size check is the simplest safe parse. Host tests in `test_hacks.cpp`. Checked on dev (7d6e17c): the first System Menu boot moved its 72-byte `hacksh_s.ini` to `hackstat.ini` (8 hacks on, log "moved the hack states to hackstat.ini (0)") and deleted the old file; `loader.ini` was kept. Open the hacks menu only after that first boot on an updated Wii: it reads the SD hack list, which may not match the old positions. lrwii still runs upstream 0.10.0.

### Dev Wii black screen: fixed (2026-11-05)

- Every autoboot of a title (cold boot, return to the menu) went black until RESET. The boot log ("Dump gecko text", now with early lines, milliseconds and launch breadcrumbs) stopped after "drive done, video not up": `VideoRegionMatches` read `rmode->viTVMode` with `rmode` NULL (video not started), a fault in a libogc 3 build; libogc's handler reloaded into the HBC stub on RESET. Fixed in 6b5a986 (video format from `CONF_GetVideo` until video is up; `ShutdownVideo` no longer frees a missing framebuffer). Confirmed on dev: a cold boot and an exit to the System Menu both work. The Balance Board theory (f5b4cf0) was wrong for this Wii, but that wrap fixes a real libogc 3.1 crash and stays.
- Installer progress text has colours (68f416e, `console_colors.h`).

### Installer: automatic mode and the Mist menu (2026-11-05)

- `Installer.dol install|update|remove` runs that action with no buttons, saves `sd:/priiloader/installer_result.txt` and returns to the loader; an unknown argument is refused before any NAND write. `update` refuses when Priiloader isn't installed. `tests/wii/installer/run.py` drives it on the bench and checks the NAND afterwards. Logic in `src/Installer/include/menu_logic.h`, host test `tests/host/installer/test_menu.cpp`.
- The menu: Mist theme (grey on black, a pale grey bar on the selected row), a band of static under the title, an occasional title flicker, and the row the bar leaves fading out over 24 frames (button 1 toggles the fade, as a test). Drawn with libogc 3.1's truecolor console escapes; the static goes straight into XFB. Not yet seen on a Wii.
- The installer now links libfat (for the result file): `-lfat` has to come before `-logc`.
- Balance Board black screen (f5b4cf0): libogc 3.1 leaves Bluetooth slots 3-5 without a wiiuse command queue; the Balance Board is slot 4. Priiloader and the installer link `src/Shared/wpadqueue.c` (from HBC reborn) with `--wrap=wiiuse_init_cmd_queue`. Checked in libogc 3.1.0's source (nothing frees the queue buffer) and the ELF (the call in `wiiuse_init` goes to the wrapper). Not yet confirmed on dev.
- The IOS patch scan was briefly changed to apply each patch once (f5b4cf0) and reverted (6bf9438): libruntimeiospatch patches every match of the same fakesign patterns across IOS memory, so patching every site is known-good practice.

### Wii checks: HTTPS and the updater (2026-11-04)

Run on two Wiis (dev 192.168.8.213 and lrwii 192.168.8.243, both from HBC, network and NAND reads only) with `tests/dolphin/tls_probe` and `tests/dolphin/update_probe` built with `NETLOG=1`. Jobs `20261104-214142-*`, `20261104-215059-2918a1`, `20261104-215818-*`.

| Kind | Change | Check |
| --- | --- | --- |
| Fix | `tls.c` `wait_socket`: a real Wii's IOS answers a `net_poll` for POLLOUT only when its timeout ends, even when the socket is ready (a 250 ms poll on a connect that took 18 to 41 ms returned ready after 250 ms; Dolphin answers at once). The connect waited the whole 20 s (probe) or 15 s (updater) timeout; github.com then answered, api.github.com closed the idle connection (`MBEDTLS_ERR_SSL_CONN_EOF`), so the updater never worked on a Wii. Polls now last at most 10 ms, repeated until the timeout. | before: api.github.com failed on both Wiis, github.com 20391 to 21468 ms, badssl hosts dropped at random; after: api.github.com 822 ms, github.com 991 ms, `tls_probe` and `update_probe` PASS on both |
| Finding | Read waits (POLLIN) wake early on the Wii: with the 20 s connect poll the handshake still ended about 0.3 to 1.4 s after it. `HTTP_Parser.cpp`'s plain sockets only wait to read; left as they are. | `tls_probe` timings |
| Finding | Crypto on the Wii: one P-256 scalar multiplication 16 ms, one P-384 40 ms (`MBEDTLS_ECP_WINDOW_SIZE 2`). Not worth tuning. | `tls_probe` timings |
| Finding | Entropy on the Wii from a DOL launched by HBC: 256 timing samples around 64 NAND reads, 62 to 65 distinct low bytes; ES_Sign -1017 (as in Dolphin), MAC and device id read. Enough for the generator to seed; ES_Sign inside the installed Priiloader is still unchecked. | `tls_probe` entropy line |
| Finding | Updater on the Wii: our release list (no releases, "no updates") in about 830 ms; the github.com 302 to release-assets.githubusercontent.com chain works; 5473689 bytes in 11 to 17 s, digest matches, a corrupted copy is refused. Every reply had a `Content-Length` and ended with close_notify, as in Dolphin. Heap high-water 724508 bytes. | `update_probe` on both Wiis |
| Finding | The Wii's clock is its local time (about 4 h off UTC here); certificate dates still check. | probe clock lines |
| Tools | The probes build with `make NETLOG=1` (output to `hbc.py run` through hbc-reborn's `sdk/hbc_netlog.h`) and close the log before `net_deinit`. | jobs above |

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

Settings and autoboot speed are in commit 50c4ba8, the TLS layer in f390a80, the updater in 06de1a4. Checked with host tests (`tests/test_host_http.py` 37 cases, `tests/test_host_update.py` 19 cases, `tests/test_host_tls.py`), a clean build with no warnings, and a Dolphin run of `tests/dolphin/update_probe` (real HTTPS to GitHub). Not yet run on a Wii.

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

Not fixed: the changelog lines keep a leading newline for CRLF text. (`PatchIOSKernel` now counts its patches and stops at the first, 50c4ba8.)

Wii checks for the updater and TLS that are left (HTTPS itself passed on two Wiis, see above):

- An update install from GitHub: attended, with a fresh BootMii NAND backup. It needs a release with `Priiloader_Update.dol` (none exists yet; see `docs/updates.md`). Check the digest refusal with a wrong file as well.
- HTTPS from the installed Priiloader (booted from NAND, not HBC): ES_Sign there, and a Wii whose clock is wrong must say so, not fail with a certificate text.
- Autoboot key wait timing after the speed changes (50c4ba8): the keys held at power-on must still be seen.

Size (priiloader.dol was 838304 bytes before TLS, 826368 before these fixes; TLS added about 240 KB):

- About 95 KB of exception tables (`.eh_frame`, `.eh_frame_hdr`); the code has 89 `try`/`catch` lines, so dropping exceptions is a refactor.
- `wii_font_Bitmap` is 97280 bytes.
- `-Os` and `-ffunction-sections` are untried.

Test layers:

- A Dolphin run with a known log line (USB Gecko output).
