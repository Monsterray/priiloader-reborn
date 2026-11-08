# priiloader-reborn agent guidance

For build, test, review, Dolphin, or Wii work, read [the priiloader-reborn skill](.agents/skills/priiloader-reborn-build-and-test/SKILL.md). Use [CONTRIBUTING.md](CONTRIBUTING.md) for setup and [REVIEW.md](REVIEW.md) for the review plan and findings.

Priiloader replaces the System Menu's boot content on NAND. A crash in its autoboot path, or an installer that leaves the TMD and the boot content out of step, can leave a Wii without a working System Menu. Treat `src/Installer`, `src/loader` and the autoboot path in `src/priiloader` as boot-critical.

- Build with `make` from the root, in the devkitPro MSYS2 shell (devkitPPC r51, libogc 3.1.0). Never update devkitPro packages as part of a change.
- Test with `python -m unittest discover -s tests` and the host C tests the skill lists. A test for a bug that is not fixed yet is marked `known_bug`; remove the mark in the commit that fixes it.
- Say which layer a result came from: host test, build, Dolphin, or the bench Wii. Dolphin results are functional only; it emulates IOS at a high level and cannot show what a NAND write does to a real System Menu.
- Wii access goes through the bench queue only (`tools/wii_targets.py`, `bench_wii()`). Never touch a protected (production) Wii. The bench Wii is queue name `devwii`; `--wii dev` is a group that also holds the living-room `lrwii`, so use `--wii devwii` (or no `--wii`) for anything that writes NAND or boot files. Installing, updating or removing Priiloader writes NAND: only with the owner at the Wii and a fresh BootMii NAND backup, never from an unattended job.
- Never convert bases, add offsets or work out sizes in your head: run it (`python -c "print(hex(553649152))"`) and quote the output. Before calling a tool's number wrong, put both raw values side by side in one base, converted by a tool; a gap of one round step (0x400, a power of two) is as often a conversion slip as a bug.
- No paths of one workstation (this PC's project and tool folders, home folders; devkitPro's standard install is fine) in code, scripts or docs. Shared tools (hbc-reborn, the bench, Dolphin) are found through `tools/wii_paths.py` and the variables in wii-bench's `docs/paths.md`; `HBC_REBORN` names the hbc-reborn checkout for the netlog header.
- Keep Wii keys and console data out of the repo. `keys/` is ignored. Check staged file names before every commit, and stage by path.
- Every text file is LF, in the repository and the checkout; only `.bat` files get CRLF in the checkout (`.gitattributes`). Merge upstream (`DacoTaco/priiloader`) with `git merge -X renormalize upstream/master`. `tests/test_line_endings.py` checks it; `tools/fix_line_endings.py` converts an old checkout. From Python, write files with `write_bytes`.
- Work on `develop`; `master` follows upstream's `master`.
- Prefer a focused fix with a matching test. Record larger ideas in `REVIEW.md` with evidence and a clear trigger.
