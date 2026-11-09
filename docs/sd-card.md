# Priiloader and the SD card

Priiloader keeps its settings and hack choices on the Wii's NAND, so a Wii needs nothing on its SD card to boot. The card
holds the installer, the hack list for the hacks menu, and a few optional files.

`python tools/make_sd.py` lays out the card from a build (in `dist/sd`); copy its contents to the root of the card.

## What goes on the card

| Path | Needed for | Example in this repository |
|---|---|---|
| `apps/priiloader/boot.dol` | installing, updating or removing Priiloader from HBC (it is the Installer) | `src/Installer/Installer.dol` after `make` |
| `apps/priiloader/meta.xml`, `icon.png` | the Installer's HBC entry | `src/Installer/META_TEMPLATE.XML` (the build fills in the version); `src/Installer/icon.png` |
| `apps/priiloader/hacks_hash.ini` | the hacks menu: Priiloader reads the hack list here first, and saves a copy to NAND | `src/priiloader/hacks_hash.ini` (the master list; the format is in `docs/HACKS.md`) |
| `password.txt` | setting a password from Priiloader's Set Password menu (optional) | below |
| `wii_secure.dol` | the DOL the password menu can install with the password (optional) | none |
| `prii.log` | the boot log, written by Priiloader when "Dump Gecko output" is on: a header for each boot with the Wii clock's date and time, then timestamped lines. Attach it to a bug report. | none: Priiloader writes it |
| `priiloader/installer_result.txt` | written by the Installer when run with `install`, `update` or `remove` | none: the Installer writes it |

`password.txt`: the first line is the password (letters and digits), the optional second line the name of the DOL to
install with it instead of `wii_secure.dol`. Priiloader copies it to NAND when you choose "Install file". A password
can lock you out of Priiloader's menu: keep a NAND backup.

```
mypassword
```

## On NAND (not on the card)

All of these are in the System Menu's data folder, `/title/00000001/00000002/data/`.

| File | What it is | Example or format |
|---|---|---|
| `loader.ini` | Priiloader's settings: a fixed-size binary record, not text | `docs/settings.ini` documents every field and its default |
| `hackshas.ini` | the copy of `hacks_hash.ini` used at boot | `src/priiloader/hacks_hash.ini` |
| `hackstat.ini` | the hacks that are on, one name per line, matched to `hackshas.ini` by name | `# priiloader: the System Menu hacks that are on, one name per line` then for example `Block Disc Updates` |
| `password.txt` | the installed password | as above |
| `main.bin`, `main.nfo` | the installed autoboot DOL or ELF and its information | none |

Priiloader before 0.10.1 kept the hack choices in `hacksh_s.ini`, one byte per hack by position. A matching one is
moved to `hackstat.ini` the first time the hacks are loaded, then deleted.
