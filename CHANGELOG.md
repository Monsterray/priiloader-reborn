# Changelog

Versions of priiloader-reborn, newest first. History before 0.10.1 is upstream's: see [DacoTaco/priiloader](https://github.com/DacoTaco/priiloader) (the old per-version notes are kept in `src/Installer/priiloader.nfo`).

Back up the NAND with BootMii before you install or update. See [README.md](README.md).

## 1.0.0 (2026-11-09)

The list below is everything since upstream 0.10. It folds in 1.0.0-rc1 (2026-11-08), the release candidate.

New since 0.10.1: a stuck USB drive no longer causes a black screen; the USB keyboard starts with the menu; the Wii Remote pointer works in the System Menu after Priiloader's menu; the boot log's header carries the date; the hack states of an older Priiloader are read correctly when the hacks menu is opened first; the changelog screen no longer starts each line with an empty line.

Checked on a real Wii: the update from 0.10.1 to 1.0.0-rc1 through Check For Update, with the sha256 check; the Wii Remote pointer in the System Menu; a cold boot from standby (red light) and from idle (yellow light), both autobooting; and RESET held at power on, which opens Priiloader's menu.

### Boot and autoboot

- A stuck USB drive no longer causes a black screen. The SD card mounts first, USB starts only with the menu, and shutting the mounts down waits at most 2 seconds. A drive left mid-transfer by a reset used to block the USB start-up, so Priiloader never autobooted.
- Autoboot of a title no longer goes black until RESET: `LaunchTitle` read the video mode before video was started.
- Autoboot is faster: one read of `state.dat` instead of two, the hack file read once, and a key scan instead of a fixed sleep. About 1.5 s to the Homebrew Channel on a real Wii.
- Autoboot crash fixes: an overflow reading `main.nfo`, the length of the arguments, the entry address check for ELF files (a returning DOL now stops instead of running System Menu patch code).
- The DVD drive: a failed async call clears the busy flag, and waits time out instead of spinning.
- The password check no longer uses freed memory or writes past its buffer.

### Installer

- A failed write restores the System Menu. A copy of the System Menu is kept when an update fails, and removed on install only after a verified restore.
- The TMD backup is kept when a restore fails. A size mismatch or a short read or write is an error, not a success. A transient error no longer turns an update into an install.
- On the vWii, Remove restores the BC-NAND copy, and deletes the copies only after the TMD restore succeeds.
- An update keeps the settings and the password.
- Automatic mode: `Installer.dol install|update|remove` runs with no buttons and writes `sd:/priiloader/installer_result.txt`. An unknown argument is refused before anything is written.
- The menu has a new look (the Mist menu), and coloured progress text.

### Updater

- Reads GitHub releases over HTTPS (mbedTLS 3.6.7; the certificate chain and the host name are checked, the Wii's clock is checked first).
- Checks the sha256 digest that GitHub lists for `Priiloader_Update.dol`. A file with no digest, or a different one, is refused.
- Release candidates show only with **Show RC Updates**.
- **Update from** picks GitHub (default) or DacoTaco's server, which keeps upstream's plain HTTP path.
- The HTTP client is bounded: at most 5 redirects, a 16 MB limit, and sockets closed on every path.
- Poll in 10 ms slices: a real Wii's IOS holds a write poll for its whole timeout.
- Fixed: the changelog screen started each line with an empty line.

### Settings

- The settings file (`loader.ini`) grows instead of resetting. Upstream's 32-byte file keeps every value and gains the new settings. See [docs/settings.ini](docs/settings.ini).
- New setting **Autoboot key wait** (0, 100, 250 or 500 ms; default 500, as before).
- New setting **Update from**.

### Hacks

- Hack choices are saved by name in `hackstat.ini`. The positional `hacksh_s.ini` of older versions is read once, against the NAND hack list, moved over, and deleted. A new hack list with another order no longer turns on the wrong hacks.
- The hack file is read once and parsed in memory.

### Input

- The Wii Remote pointer works in the System Menu after Priiloader's menu: the remotes are disconnected first, so the System Menu sees them connect.
- The USB keyboard starts with the menu, not before the autoboot. The Wii Remote, the GameCube pad and the console buttons still stop an autoboot.
- A Balance Board or a fourth Wii Remote no longer crashes Priiloader (a libogc 3.1 queue bug, worked around).

### Fixes

- A boot log (`Dump Gecko output`, in `sd:/prii.log`) with early lines, milliseconds, a header for each boot with the Wii clock's date and time, launch breadcrumbs, and a note when a key wait stalls. Lines are kept while the card is unmounted.
- Hack states of an old `hacksh_s.ini` are read correctly even when the hacks menu is opened first.
- Many smaller defects: play-log title IDs, a TMD size check, leaks, the disc partition count, `meta.xml` with empty arguments, `mem2` allocation checks, a mount thread deadlock, IOS patches (every site patched while a patch is missing).
- The build keeps `priiloader.app` from a stale tag: the Makefile strips a leading "v" (OpenDolBoot rejects it) and fails when no app is written.

### Building and testing

- Builds with no warnings on devkitPro r51 and libogc 3.1.0 (libogc 3.2.1 is not supported: a `WPAD_Shutdown` use-after-free).
- Host tests (HTTP client, updater, installer against a simulated NAND with injected failures, hacks, settings, password, TLS), also under AddressSanitizer.
- Dolphin tests for TLS and the updater.
- `tools/make_sd.py` lays out the SD card and, with `--release`, the release files.
- Every text file is LF. No paths of one workstation in code or docs; shared tools are found through `tools/wii_paths.py`. `keys/` is never committed.
- Contributor guide, docs index and a settings reference.

## 1.0.0-rc1 (2026-11-08)

Release candidate for 1.0.0, folded into 1.0.0 above. Its pre-release page is on GitHub.

## 0.10.1 (2026-11-06)

The first priiloader-reborn release: upstream 0.10 with the boot and NAND safety fixes, the settings that survive updates, hack states by name, the installer's automatic mode, the fix for the black screen on autoboot of a title, the boot log, and the GitHub updater over HTTPS as the default source. It is the base for testing the update to 1.0.0.

## Before 0.10.1

Upstream history: [DacoTaco/priiloader](https://github.com/DacoTaco/priiloader/releases). The last upstream version this fork builds on is 0.10 (Version Kappa).
