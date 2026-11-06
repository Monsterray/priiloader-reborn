"""Lays out what goes on a Wii's SD card for Priiloader, from the build in this checkout, and the release files:

    python tools/make_sd.py [OUT]          (OUT defaults to dist/sd)
    python tools/make_sd.py --release      dist/sd, then dist/release (below)

    OUT/apps/priiloader/boot.dol        the Installer (src/Installer/Installer.dol): HBC lists it as "Priiloader"
    OUT/apps/priiloader/meta.xml        its HBC entry (src/Installer/META.XML, filled in by the build)
    OUT/apps/priiloader/icon.png
    OUT/apps/priiloader/hacks_hash.ini  the System Menu hacks; Priiloader's hacks menu reads it from here

    dist/release/Priiloader_Update.dol             the asset the updater downloads (the same Installer)
    dist/release/priiloader-reborn-VERSION-sd.zip  the SD card files, to unzip at the root of the card
    dist/release/SHA256SUMS

Copy OUT's contents to the root of the card, or to a bench Wii with
    hbc.py sync OUT/apps/priiloader sd:/apps/priiloader
Nothing else is needed on the card. Priiloader's settings and hack choices live on NAND; the files it can also use
from the card (password.txt to set a password, the log prii.log) are described in docs/sd-card.md.
"""
import hashlib
import pathlib
import re
import shutil
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "src" / "Installer"


def version():
    text = (ROOT / "src" / "Shared" / "version.h").read_text()
    parts = {k: int(v) for k, v in re.findall(r"#define VERSION_(MAJOR|MINOR|PATCH|RC)\s+(\d+)", text)}
    v = f"{parts['MAJOR']}.{parts['MINOR']}.{parts['PATCH']}"
    return v + (f"-rc{parts['RC']}" if parts.get("RC") else "")


def make_sd(out):
    dol = INSTALLER / "Installer.dol"
    meta = INSTALLER / "META.XML"
    if not dol.exists() or not meta.exists():
        sys.exit("build first: make")
    app = out / "apps" / "priiloader"
    app.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(dol, app / "boot.dol")
    shutil.copyfile(meta, app / "meta.xml")
    shutil.copyfile(INSTALLER / "icon.png", app / "icon.png")
    shutil.copyfile(ROOT / "src" / "priiloader" / "hacks_hash.ini", app / "hacks_hash.ini")
    for f in sorted(app.iterdir()):
        print(f"{f.relative_to(out).as_posix()}  {f.stat().st_size} bytes")
    return app


def make_release(sd):
    rel = ROOT / "dist" / "release"
    if rel.exists():
        shutil.rmtree(rel)
    rel.mkdir(parents=True)
    shutil.copyfile(INSTALLER / "Installer.dol", rel / "Priiloader_Update.dol")
    archive = rel / f"priiloader-reborn-{version()}-sd.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted((sd / "apps").rglob("*")):
            if f.is_file():
                z.write(f, f.relative_to(sd).as_posix())
    sums = "".join(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.name}\n"
                   for f in sorted(rel.iterdir()))
    (rel / "SHA256SUMS").write_bytes(sums.encode())
    print(sums, end="")


def main():
    args = sys.argv[1:]
    release = "--release" in args
    args = [a for a in args if a != "--release"]
    out = pathlib.Path(args[0]) if args else ROOT / "dist" / "sd"
    if release and args:
        sys.exit("--release always uses dist/sd")
    make_sd(out)
    if release:
        make_release(out)


if __name__ == "__main__":
    main()
