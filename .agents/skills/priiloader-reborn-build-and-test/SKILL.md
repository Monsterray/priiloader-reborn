---
name: priiloader-reborn-build-and-test
description: Build, test, review and hardware-check priiloader-reborn. Use for any code change, a failing build or test, adding a test, a Dolphin run, and before any work that touches a Wii. Covers the Installer (NAND, TMD, ticket), the loader, the priiloader app (autoboot, System Menu hacks, password, settings, HTTP update, titles, DVD) and the host tools OpenDolBoot and bootloader.
---

# priiloader-reborn build and test

## Approach

- Trace the path from input to output before changing code. The main paths:
  - Install: `src/Installer` copies the System Menu boot content to `1xxxxxxx.app`, writes `priiloader.app` over the boot content, and patches the TMD hash (vWii: a nandloader content instead).
  - Boot: the nandloader in `priiloader.app` starts priiloader, which reads `state.dat` and its settings, then autoboots (System Menu with hacks, a DOL from `main.bin`/`main.nfo`, HBC, BootMii) or shows its menu.
  - System Menu boot: `SystemMenu.cpp` loads the copied boot content, applies hacks from `hacks_hash.ini` and `hackstat.ini` (by name; the old `hacksh_s.ini` is migrated), and jumps through `src/loader`.
- Every file priiloader reads (NAND, SD, USB, disc, HTTP) is untrusted: a bad size must fall back to the System Menu, never crash.
- Fix the smallest shared cause. Write the test first, at the lowest layer that shows the bug, marked `known_bug` until the fix lands.
- Keep verified defects apart from ideas. Ideas go in `REVIEW.md` with evidence and a trigger.

## Build

From the devkitPro MSYS2 shell, at the repository root:

```sh
make            # loader, bootloader, OpenDolBoot, priiloader, Installer, in that order
make clean
```

From Git Bash on Windows, the same through that shell:

```sh
MSYSTEM=MSYS CHERE_INVOKING=1 TMP=/tmp TEMP=/tmp /c/devkitPro/msys2/usr/bin/bash.exe -lc 'cd "<repo>" && make'
```

- Toolchain: devkitPPC r51 (gcc 16.2) and libogc 3.1.0 from devkitPro's pacman. libogc 3.2.1 has a `WPAD_Shutdown` use-after-free; stay on 3.1.0.
- Set `TMP` to a POSIX path (`/tmp`). A Windows path with backslashes breaks the build's `sed` steps.
- `tools/bootloader` and `tools/OpenDolBoot` are host C++ tools. The devkitPro shell builds them with its own g++. A Git Bash (MINGW) shell builds them with `x86_64-w64-mingw32-g++` if present, else LLVM's `clang++`; override with `HOSTCXX=` (bootloader). Both compilers give byte-identical outputs.
- Outputs: `src/priiloader/priiloader.dol` (bootable directly, also in Dolphin), `src/Installer/data/priiloader.app` (the NAND content, made by OpenDolBoot), `src/Installer/Installer.dol` (embeds `priiloader.app`, so priiloader builds first).
- The build has no compiler warnings. Keep it that way.

## Tests

```sh
python -m unittest discover -s tests
```

- `tests/test_line_endings.py`: LF everywhere, `.gitattributes` covers every binary.
- `tests/host/hostcheck.h` is the C runner for host tests: `CHECK`, `known_bug`, and `hc_hang()`. Host tests build project sources unchanged for the PC with a stand-in for the ISFS/IOS layer (`tests/test_host_*.py`, sources in `tests/host/`; they need clang++ and are skipped without it). They cover the parsers (`hacks.cpp`, settings, `password.cpp`, `main.nfo`, `HTTP_Parser.cpp`), the updater, TLS, and the installer's NAND sequence against a simulated NAND with injected failures.
- cppcheck, as upstream CI runs it: `cppcheck . --std=c99 --std=c++11 --enable=warning,performance,portability,style --suppress=unusedFunction --suppress=unusedStructMember -UDEBUG_MEM2_LEVEL`. Style notes only today.

## Dolphin

```sh
python tools/dolphin_run.py src/priiloader/priiloader.dol --seconds 60 --expect 'TODO: a line priiloader logs at its menu'
```

- Dolphin tests (`tests/test_dolphin_*.py`) run only with `HC_DOLPHIN=1`; Dolphin is found by `tools/wii_paths.py` (`DOLPHIN`, `~/.wii-paths.json`). The `--expect` line for `priiloader.dol` is not chosen yet: priiloader logs through USB Gecko (`gecko.cpp`).
- Booting the DOL directly does not give the installed title's NAND identity or the nandloader path. Dolphin cannot show what the installer does to a real System Menu.

## Wii bench

- Queue jobs only (`wii_targets.bench_wii()`), never a protected Wii. lrwii takes only `--wii lrwii` jobs and holds real data.
- Do not run `priiloader.dol` from HBC on a Wii with another Priiloader installed: its `LoadSettings` rewrites `/title/00000001/00000002/data/loader.ini` (32 to 34 bytes, and the version fields), and the installed Priiloader then resets every setting, autoboot included (the bench's boot chain autoboots HBC). Installing, updating or removing Priiloader writes NAND and needs the owner at the Wii with a fresh BootMii NAND backup.
- The installer runs with no buttons when given an argument: `Installer.dol install|update|remove`. It writes `sd:/priiloader/installer_result.txt` (action, result ok or error, message, version, rev) and returns to HBC. `python tests/wii/installer/run.py update --attended` (a queue job, with the owner at the Wii) sends it, reads the result, runs `tests/wii/nand_state` and checks the boot content is this build's `priiloader.app` (and, for an update, that `loader.ini` survived). Without an argument the installer shows its menu (D-pad, A; `+`, `-` and HOME still work; button 1 turns the row fade off and on).
- Dolphin's frame dump gives no pictures of the installer: it draws on the CPU into XFB, and none of real XFB, immediate XFB or a visible window made Dolphin write frames. Look at the menu on the bench's capture card instead.
- Unattended jobs may run the probes in `tests/dolphin` (network, NAND reads only). Build them with `make NETLOG=1` (`*_netlog.dol`, through hbc-reborn's `sdk/hbc_netlog.h`) and run them with `hbc.py run`, so their output reaches the PC:
  `python <hbc-reborn>/tools/wii-bench/wiibench.py add --wii lrwii --timeout 480 -- python <hbc-reborn>/tools/hbc.py run --timeout 420 tests/dolphin/tls_probe/tls_probe_netlog.dol`
- A real Wii's IOS differs from Dolphin: a `net_poll` for POLLOUT returns only when its timeout ends (Dolphin answers at once), and `SO_ERROR` answers -22 on both.

## Review and release

- Report whether a check covered a host test, a build, a Dolphin run, or the bench Wii.
- The version comes from `git describe` (`src/Shared/GitVersion.mk`) and is written into the nandloader header by OpenDolBoot `-a`. The release procedure is in `docs/updates.md`; `CHANGELOG.md` gets an entry for each release.
