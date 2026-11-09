# Contributing to priiloader-reborn

This guide takes you from nothing to a built, tested Priiloader on Windows, Linux or macOS. For what the project does, read [README.md](README.md). For the review plan and open items, read [REVIEW.md](REVIEW.md).

Priiloader writes the Wii's NAND. Read [Testing on a real Wii](#testing-on-a-real-wii) before you run anything on hardware.

## 1. Install the tools

| Tool | For | Needed |
| --- | --- | --- |
| [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `wii-dev` group (devkitPPC, libogc) | building the Wii programs | yes |
| a host C++ compiler (g++ or clang++) | the host tools `tools/bootloader` and `tools/OpenDolBoot`, run during the build | yes |
| Git | the checkout and the version string (`git describe`) | yes |
| Python 3.10 or newer | `tools/*.py` and the tests | yes |
| clang and clang++ | the host C/C++ tests (skipped without it) | for tests |
| cppcheck | the static check upstream CI runs | optional |
| Dolphin | the Dolphin tests | optional |

**Pin the toolchain.** This project is built and tested on devkitPro **r51** (gcc 16.2) with **libogc 3.1.0**. libogc 3.2.1 has a use-after-free in `WPAD_Shutdown` and was rolled back, so stay on 3.1.0 until that is fixed. After you install, hold the package back in `pacman.conf`:

```
IgnorePkg = libogc
```

Do not update devkitPro packages as part of a change. The build has no compiler warnings; keep it that way.

### Linux and macOS

Install devkitPro's pacman as its [getting started page](https://devkitpro.org/wiki/Getting_Started) says, then install the Wii group (`sudo dkp-pacman -S wii-dev`; plain `pacman` in the devkitPro Docker image) and set the environment:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
```

Put those two lines in your shell profile. The system g++ builds the host tools.

### Windows

Run devkitPro's installer and choose the Wii group. Build from its **MSYS2 shell**: its g++ builds the host tools, and `DEVKITPPC` is already set. From Git Bash the host tools build with a mingw g++ (`x86_64-w64-mingw32-g++`) or with LLVM's `clang++`; override the choice with `HOSTCXX=`.

## 2. Get the source

```sh
git clone https://github.com/Monsterray/priiloader-reborn.git
cd priiloader-reborn
git remote add upstream https://github.com/DacoTaco/priiloader.git
git fetch --tags
git checkout develop
```

The version string comes from `git describe`, so keep the tags.

## 3. Build

From the repository root, in the shell above:

```sh
make
make clean      # removes the build output
```

On Windows, from Git Bash, you can reach the devkitPro shell like this. Keep `TMP` a POSIX path: a Windows path with backslashes breaks the build's `sed` steps.

```sh
MSYSTEM=MSYS CHERE_INVOKING=1 TMP=/tmp TEMP=/tmp /c/devkitPro/msys2/usr/bin/bash.exe -lc 'cd "<repo>" && make'
```

`make` builds these, in order:

| Folder | Makes | What it is |
| --- | --- | --- |
| `src/loader` | `loader.bin` | the small program that jumps into the System Menu |
| `tools/bootloader` | host tool | builds the NAND boot code around the loader |
| `tools/OpenDolBoot` | host tool | turns `priiloader.dol` into `priiloader.app`, the NAND content |
| `src/third_party/mbedtls`, `src/priiloader/tls` | libraries | TLS for the updater |
| `src/priiloader` | `priiloader.dol` | Priiloader itself. A DOL can be started directly, also in Dolphin |
| `src/Installer` | `Installer.dol`, `META.XML` | the installer. It embeds `priiloader.app` (so Priiloader builds first). `META.XML` is filled in from `META_TEMPLATE.XML` with the version and the build date |

The build writes `src/Shared/gitrev.h` (the revision). Delete it and `src/Installer/META.XML` before a release build so the tag shows in the version.

### The SD card layout

```sh
python tools/make_sd.py              # dist/sd: the files for the root of an SD card
python tools/make_sd.py --release    # also dist/release: Priiloader_Update.dol, the sd zip, SHA256SUMS
```

`apps/priiloader` in that folder is what you copy to a Wii's card: the installer as `boot.dol`, its icon and `meta.xml`, and `hacks_hash.ini`. The layout of the card and of the NAND is in [docs/sd-card.md](docs/sd-card.md).

## 4. Test

Make a Python virtual environment in `.venv` (Git ignores it). The tests need only the standard library:

```sh
python -m venv .venv
. .venv/bin/activate                 # Windows: .venv\Scripts\activate
python -m unittest discover -s tests -p "test_*.py"
```

| Check | Where | Needs |
| --- | --- | --- |
| Line endings, `.gitattributes` coverage | `tests/test_line_endings.py` | Python |
| No workstation paths in tracked files | `tests/test_workstation_paths.py` | Python, git |
| Host tests: HTTP client, update logic, installer against a simulated NAND, hacks, settings, password, TLS | `tests/test_host_*.py`, sources in `tests/host/` | `clang++` and `clang` |
| Dolphin: TLS and the updater against real servers | `tests/test_dolphin_*.py`, programs in `tests/dolphin/` | Dolphin, internet, a built probe DOL |
| Static check | `cppcheck . --std=c99 --std=c++11 --enable=warning,performance,portability,style --suppress=unusedFunction --suppress=unusedStructMember -UDEBUG_MEM2_LEVEL` | cppcheck |
| Install, update, removal | by hand, on a Wii | a Wii and a BootMii backup |

**Host tests** build project sources unchanged for the PC, against stand-ins for the ISFS/IOS layer. They look for `clang++` in `HOSTCXX`, then on `PATH` (on Windows also LLVM's standard folder), and are skipped when it is not found. `HC_ASAN=1` adds AddressSanitizer. The runner is `tests/host/hostcheck.h`. A test marked `known_bug` describes a bug that is still there: it is reported as `known` and does not fail the run. When you fix the bug, remove the mark; a `known_bug` that passes fails the run (`FIXED?`) as a reminder.

**Dolphin tests** are optional. They are skipped unless you set `HC_DOLPHIN=1`, Dolphin is found, and the probe DOL is built (`make -C tests/dolphin/tls_probe`, and the same for `update_probe`). To run a DOL by hand:

```sh
python tools/dolphin_run.py src/priiloader/priiloader.dol --seconds 60
```

`tools/wii_paths.py` finds the shared tools: an environment variable first, then `~/.wii-paths.json` (for example `{"DOLPHIN": "D:/emu/Dolphin/Dolphin.exe"}`), then a portable default. Never write a path of your own PC into a script or a document.

| Variable | Names | Default |
| --- | --- | --- |
| `DOLPHIN` | the Dolphin executable | `Dolphin` or `dolphin-emu` on `PATH`, then the standard install |
| `WII_PATHS_FILE` | another file instead of `~/.wii-paths.json` | the file in your home folder |
| `HBC_REBORN` | an hbc-reborn checkout (its `tools/hbc.py`; the probes' netlog header) | `../hbc-reborn` beside this checkout |
| `HBC_PY` | `hbc.py` itself | `$HBC_REBORN/tools/hbc.py` |
| `WII_BENCH` | a bench queue's `wiibench.py` (the maintainer's test Wiis; contributors do not need it) | none |
| `HOSTCXX` | the host C++ compiler for the host tools and host tests | found on `PATH` |

Dolphin emulates IOS at a high level: its results are functional only. It cannot show what a NAND write does to a real System Menu.

## 5. Conventions

- **Branches.** Work on `develop` and open pull requests against `develop`. `master` follows upstream's `master` and moves at a release.
- **Commits.** A focused change with a matching test. Say what the change was checked with: a host test, a build, Dolphin, or a Wii.
- **Line endings.** Every text file is LF, in the repository and in the checkout (`.gitattributes`; `.bat` files get CRLF in the checkout). Merge upstream with `git merge -X renormalize upstream/master`. For a checkout made before this rule: `python tools/fix_line_endings.py`.
- **Boot safety.** The Installer, the loader and Priiloader's autoboot path decide whether a Wii reaches its System Menu. Bad input (a short file, a wrong size, a failed NAND write) must end in the System Menu or a clean error, never a crash or a half-written NAND. Every file Priiloader reads (NAND, SD, USB, disc, HTTP) is untrusted.
- **Keys and console data stay out of Git.** `keys/` is ignored. Check `git status` and stage by path before you commit.
- **No paths of your PC** in code, scripts or docs.
- **Findings and ideas** go in [REVIEW.md](REVIEW.md), with evidence and a trigger.
- **AI agents** read `AGENTS.md` and `.agents/skills/priiloader-reborn-build-and-test/SKILL.md`.

## Testing on a real Wii

Installing, updating or removing Priiloader writes the NAND.

1. Make a fresh BootMii NAND backup first, and keep a copy off the Wii.
2. Use a Wii you can afford to lose, and stay at it for the run.
3. Install with the **Installer** (`Installer.dol`, or `apps/priiloader` from `tools/make_sd.py`). It also runs with no buttons: `Installer.dol install|update|remove` writes its result to `sd:/priiloader/installer_result.txt`.
4. **Never run `priiloader.dol` from the Homebrew Channel on a Wii that has another Priiloader installed.** It migrates `loader.ini` (32 to 34 bytes), and the installed Priiloader then resets every setting, autoboot included. A DOL run is fine on a Wii without Priiloader.
5. A DOL started directly has no NAND identity of the installed title. Check the DOL and the installed build as two separate things.

**The boot log is the first tool for a boot bug.** Turn on `Dump Gecko output` in Priiloader's settings. Priiloader writes `sd:/prii.log`, with early lines, milliseconds, a header for each boot with the Wii clock's date and time, and a breadcrumb for each launch. The same text goes to a USB Gecko.

## Releasing

The maintainer's procedure is in [docs/updates.md](docs/updates.md): set the version in `src/Shared/version.h`, tag `vX.Y.Z` (release candidates `vX.Y.Z-rcN`), build, run `python tools/make_sd.py --release`, and publish the assets on GitHub. The updater only reads a release that carries `Priiloader_Update.dol` and a sha256 digest.
