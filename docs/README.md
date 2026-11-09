# Priiloader documentation

| Document | For | What is in it |
| --- | --- | --- |
| [FAQ.md](./FAQ.md) | users | Opening the menu, updating, reporting a boot bug, vWii notes |
| [HACKS.md](./HACKS.md) | hack authors | The `hacks_hash.ini` format: versions, offsets, hashes, master hacks |
| [HACKSLIST.md](./HACKSLIST.md) | users | What each hack in the shipped list does |
| [sd-card.md](./sd-card.md) | users, testers | What goes on the SD card, and what Priiloader keeps on NAND (`loader.ini`, `hackstat.ini`, `password.txt`, `main.bin`) |
| [settings.ini](./settings.ini) | developers | Every setting: offset, size, values, default and menu text. A host test keeps it in step with the code |
| [updates.md](./updates.md) | users, maintainers | How the updater picks a version, what its error messages mean, and how to cut a release |

Elsewhere in the repository:

- [README.md](../README.md): what Priiloader is, install, use.
- [CHANGELOG.md](../CHANGELOG.md): what changed in each version.
- [CONTRIBUTING.md](../CONTRIBUTING.md): set up, build, test, send a change.
- [REVIEW.md](../REVIEW.md): the review plan, findings and open items.
- `src/Installer/ReadMe.txt`: the installer's notes.
