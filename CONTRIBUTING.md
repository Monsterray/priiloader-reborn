# Contributing to priiloader-reborn

## Set up

Install devkitPro with devkitPPC and libogc (pacman group `wii-dev`). The build is checked with devkitPPC r51 and libogc 3.1.0; libogc 3.2.1 has a `WPAD_Shutdown` use-after-free, so hold it back (`IgnorePkg = libogc` in pacman.conf) until a fixed release.

- **Linux, macOS:** the devkitPro packages plus the system g++ for the host tools.
- **Windows:** the devkitPro installer. Build from its MSYS2 shell, whose g++ builds the host tools. From Git Bash, the host tools build with a mingw g++ or with LLVM's `clang++`.

```sh
git clone <this repository> priiloader-reborn
cd priiloader-reborn
git remote add upstream https://github.com/DacoTaco/priiloader.git
```

The Python checks (`tools/*.py`, `tests/*.py`) need only Python 3.10+.

## Build

```sh
make
```

On Windows, from Git Bash:

```sh
MSYSTEM=MSYS CHERE_INVOKING=1 TMP=/tmp TEMP=/tmp /c/devkitPro/msys2/usr/bin/bash.exe -lc 'cd "<repo>" && make'
```

Keep `TMP` a POSIX path: a Windows path with backslashes breaks the build's `sed` steps.

## Test

| Check | Command | Needs |
| --- | --- | --- |
| Line endings and `.gitattributes` coverage | `python -m unittest discover -s tests` | Python |
| No workstation paths in tracked files (shared tools go through `tools/wii_paths.py`) | `tests/test_workstation_paths.py` (same command) | Python, git |
| Static check (as upstream CI) | `cppcheck . --std=c99 --std=c++11 --enable=warning,performance,portability,style --suppress=unusedFunction --suppress=unusedStructMember -UDEBUG_MEM2_LEVEL` | cppcheck |
| Host tests | not written yet (REVIEW.md, Next) | a C compiler |
| Dolphin run | `python tools/dolphin_run.py src/priiloader/priiloader.dol --seconds 60` | Dolphin |
| Install, update, removal | by hand, on a Wii with a fresh BootMii NAND backup | a Wii |

A test marked `known_bug` describes behaviour that is still broken: it is reported as `known` and doesn't fail the run. When you fix the bug, remove the mark; a `known_bug` test that passes fails the run (`FIXED?`) as a reminder.

## Conventions

- **Branches.** Work on `develop`. `master` follows upstream's `master`.
- **Line endings.** Every text file is LF, in the repository and in the checkout; `.bat` files are checked out with CRLF, which cmd.exe needs. Merge upstream with `git merge -X renormalize upstream/master`. For a checkout made before this rule: `python tools/fix_line_endings.py`.
- **Boot safety.** The Installer, the loader and priiloader's autoboot path decide whether a Wii reaches its System Menu. Bad input (a short file, a wrong size, a failed NAND write) must end in the System Menu or a clean error, never a crash or a half-written NAND.
- **Evidence.** Say what a change was checked with: a host test, a build, Dolphin, or a Wii. Record findings and ideas in `REVIEW.md`.
- **No keys or console data.** `keys/` is ignored; check `git status` before you commit.
- **AI agents** read `AGENTS.md` and `.agents/skills/priiloader-reborn-build-and-test/SKILL.md`.
