"""Runs nand_state.dol on the bench Wii and prints its report. A wii-bench queue job, attended only: the probe patches
IOS in memory as the installer does, which can hang IOS.

    python tests/wii/nand_state/run.py LABEL

The probe reloads IOS, which drops the network, so it saves its report to sd:/priiloader-tests/nand_state_LABEL.txt;
this sends the DOL, waits for HBC to answer again, and fetches that file.
"""
import pathlib
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "tools"))
import wii_targets  # noqa: E402


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "now"
    wii = wii_targets.bench_wii("tests/wii/nand_state/run.py")
    remote = f"sd:/priiloader-tests/nand_state_{label}.txt"
    wii_targets.hbc(wii, "rm", remote, timeout=30)
    r = wii_targets.hbc(wii, "send", HERE / "nand_state.dol", label, timeout=60, show=True)
    if r.rc:
        sys.exit("send failed")
    time.sleep(10)
    end = time.monotonic() + 120
    while time.monotonic() < end:
        if wii_targets.in_hbc(wii):
            break
        time.sleep(3)
    else:
        sys.exit("HBC did not come back within 130 s: look at the TV")
    with tempfile.TemporaryDirectory() as tmp:
        local = pathlib.Path(tmp) / "report.txt"
        r = wii_targets.hbc(wii, "get", remote, local, timeout=60)
        if r.rc or not local.exists():
            sys.exit(f"no report on the SD card ({r.text[-200:]})")
        print(local.read_text(errors="replace"))


if __name__ == "__main__":
    main()
