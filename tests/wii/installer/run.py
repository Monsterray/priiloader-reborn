"""Runs the Installer on the bench Wii with no buttons, then checks the System Menu's NAND files.
A wii-bench queue job, attended only: it writes the System Menu title on NAND and patches IOS in memory, which can
hang IOS. Have a BootMii NAND backup and someone at the Wii.

    python tests/wii/installer/run.py install|update|remove --attended

The installer runs the action from its command line (menu_logic.h), saves sd:/priiloader/installer_result.txt
and returns to HBC by itself. This sends src/Installer/Installer.dol with the action, waits for HBC to answer again,
reads the result, then runs tests/wii/nand_state and checks:
  install, update: the boot content is this build's priiloader.app (src/Installer/data/priiloader.app)
  update: loader.ini is still there
  remove: the boot content is no longer this build's, and there's no title_or.tmd left
Exit status 0 only when the installer said ok and every check passed.
"""
import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import wii_targets  # noqa: E402

DOL = ROOT / "src" / "Installer" / "Installer.dol"
APP = ROOT / "src" / "Installer" / "data" / "priiloader.app"
RESULT = "sd:/priiloader/installer_result.txt"


def wait_for_hbc(wii, secs):
    time.sleep(10)
    end = time.monotonic() + secs
    while time.monotonic() < end:
        if wii_targets.in_hbc(wii):
            return True
        time.sleep(3)
    return False


def read_result(wii):
    with tempfile.TemporaryDirectory() as tmp:
        local = pathlib.Path(tmp) / "result.txt"
        r = wii_targets.hbc(wii, "get", RESULT, local, timeout=60)
        if r.rc or not local.exists():
            return None
        text = local.read_text(errors="replace")
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def nand_state(label):
    run = subprocess.run([sys.executable, str(ROOT / "tests" / "wii" / "nand_state" / "run.py"), label],
                         capture_output=True, text=True, timeout=300)
    return run.returncode, run.stdout + run.stderr


def main():
    args = sys.argv[1:]
    if len(args) != 2 or args[0] not in ("install", "update", "remove") or args[1] != "--attended":
        sys.exit(__doc__)
    action = args[0]
    if not DOL.exists() or not APP.exists():
        sys.exit("build first: make")

    wii = wii_targets.bench_wii("tests/wii/installer/run.py")
    expected = hashlib.sha1(APP.read_bytes()).hexdigest()
    print(f"installer: {action}, this build's priiloader.app sha1 {expected}")

    wii_targets.hbc(wii, "rm", RESULT, timeout=30)
    r = wii_targets.hbc(wii, "send", DOL, action, timeout=90, show=True)
    if r.rc:
        sys.exit("send failed")
    if not wait_for_hbc(wii, 240):
        sys.exit("HBC did not come back within 250 s: look at the TV")

    result = read_result(wii)
    if result is None:
        sys.exit(f"no {RESULT} on the SD card: the installer didn't finish (look at the TV)")
    print("result:", result)
    failures = []
    if result.get("action") != action:
        failures.append(f"result is for {result.get('action')!r}, not {action!r}")
    if result.get("result") != "ok":
        failures.append(f"installer said {result.get('result')!r}: {result.get('message')}")

    code, report = nand_state(f"after_{action}")
    print(report)
    if code:
        failures.append("nand_state failed")
    else:
        boot = re.search(r"content index \d+ id ([0-9a-f]{8}) .* sha1 ([0-9a-f]{40})  <- boot", report)
        boot_sha = boot.group(2) if boot else None
        #the TMD's hash for the boot content, and the content file's own hash
        boot_file = re.search(r"%s\.app +\d+ bytes sha1 ([0-9a-f]{40})" % boot.group(1), report) if boot else None
        file_sha = boot_file.group(1) if boot_file else None
        if action in ("install", "update") and (boot_sha != expected or file_sha != expected):
            failures.append(f"boot content sha1 {file_sha} (TMD {boot_sha}), expected this build's {expected}")
        if action == "update" and "loader.ini: error" in report:
            failures.append("loader.ini is gone after an update")
        if action == "remove":
            if boot_sha == expected:
                failures.append("the boot content is still this build's priiloader.app")
            if "title_or.tmd" in report:
                failures.append("title_or.tmd is still there")

    for f in failures:
        print("FAIL:", f)
    print("installer run:", "ok" if not failures else "FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
