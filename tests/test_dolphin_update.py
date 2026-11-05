"""Runs the update probe (tests/dolphin/update_probe) in Dolphin: the GitHub updater path over real HTTPS: releases of our repo and Mbed-TLS,
a download through GitHub redirects checked against its sha256 digest. A functional check only (Dolphin's IOS network is a host socket
bridge): it needs the internet, a Dolphin install and a built update_probe.dol, so it is skipped unless HC_DOLPHIN
is set. Build the DOL first: make -C tests/dolphin/update_probe (devkitPro shell).
"""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
PROBE = os.path.join(ROOT, "tests", "dolphin", "update_probe", "update_probe.dol")


class DolphinUpdateTests(unittest.TestCase):
    def test_probe(self):
        if not os.environ.get("HC_DOLPHIN"):
            self.skipTest("set HC_DOLPHIN=1 to run Dolphin tests")
        import dolphin_run
        if dolphin_run.find_dolphin() is None:
            self.skipTest("Dolphin not found")
        if not os.path.exists(PROBE):
            self.skipTest("update_probe.dol is not built")
        run = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "dolphin_run.py"), PROBE, "--seconds", "150",
                              "--expect", "UPDATEPROBE: RESULT PASS", "--fail", "UPDATEPROBE: RESULT FAIL"],
                             capture_output=True, text=True, timeout=400, cwd=ROOT)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
