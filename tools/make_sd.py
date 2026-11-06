"""Lays out what goes on a Wii's SD card for Priiloader, from the build in this checkout:

    python tools/make_sd.py [OUT]          (OUT defaults to dist/sd)

    OUT/apps/priiloader/boot.dol        the Installer (src/Installer/Installer.dol): HBC lists it as "Priiloader"
    OUT/apps/priiloader/meta.xml        its HBC entry, with this build's version
    OUT/apps/priiloader/icon.png
    OUT/apps/priiloader/hacks_hash.ini  the System Menu hacks; Priiloader's hacks menu reads it from here

Copy OUT's contents to the root of the card, or to a bench Wii with
    hbc.py sync OUT/apps/priiloader sd:/apps/priiloader
Nothing else is needed on the card. Priiloader's settings and hack choices live on NAND; the files it can also use
from the card (password.txt to set a password, the log prii.log) are described in docs/sd-card.md.
"""
import datetime
import pathlib
import re
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape

ROOT = pathlib.Path(__file__).resolve().parents[1]

META = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>Priiloader</name>
  <coder>DacoTaco, BadUncle and the priiloader-reborn contributors</coder>
  <version>{version}</version>
  <release_date>{date}</release_date>
  <short_description>Install, update or remove Priiloader</short_description>
  <long_description>Priiloader starts before the System Menu: hold RESET while the Wii powers on to reach its menu, where it can autoboot a channel or an app, apply System Menu hacks, and recover a Wii whose System Menu does not start.

This installer installs, updates or removes Priiloader. Updating keeps your settings and hacks.

Run it with the argument install, update or remove (for example from wiiload) to do that with no buttons; the result goes to sd:/priiloader/installer_result.txt.

Installing or updating a System Menu removes Priiloader: install it again afterwards.</long_description>
  <no_ios_reload/>
</app>
"""


def version():
    text = (ROOT / "src" / "Shared" / "version.h").read_text()
    parts = {k: int(v) for k, v in re.findall(r"#define VERSION_(MAJOR|MINOR|PATCH|RC)\s+(\d+)", text)}
    v = f"{parts['MAJOR']}.{parts['MINOR']}.{parts['PATCH']}"
    if parts.get("RC"):
        v += f"-rc{parts['RC']}"
    rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return f"{v} ({rev})" if rev else v


def commit_date():
    out = subprocess.run(["git", "log", "-1", "--format=%cd", "--date=format:%Y%m%d%H%M%S"],
                         cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return out or datetime.datetime.now().strftime("%Y%m%d%H%M%S")


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "dist" / "sd"
    dol = ROOT / "src" / "Installer" / "Installer.dol"
    if not dol.exists():
        sys.exit("build first: make")
    app = out / "apps" / "priiloader"
    app.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(dol, app / "boot.dol")
    shutil.copyfile(ROOT / "src" / "Installer" / "icon.png", app / "icon.png")
    shutil.copyfile(ROOT / "src" / "priiloader" / "hacks_hash.ini", app / "hacks_hash.ini")
    (app / "meta.xml").write_bytes(META.format(version=escape(version()), date=commit_date()).encode())
    for f in sorted(app.iterdir()):
        print(f"{f.relative_to(out).as_posix()}  {f.stat().st_size} bytes")


if __name__ == "__main__":
    main()
