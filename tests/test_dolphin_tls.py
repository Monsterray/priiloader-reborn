"""Runs the TLS probe (tests/dolphin/tls_probe) in Dolphin: real HTTPS to api.github.com and github.com, and
certificate refusals against badssl.com. A functional check only (Dolphin's IOS network is a host socket
bridge): it needs the internet, a Dolphin install and a built tls_probe.dol, so it is skipped unless HC_DOLPHIN
is set. Build the DOL first: make -C tests/dolphin/tls_probe (devkitPro shell).
"""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
PROBE = os.path.join(ROOT, "tests", "dolphin", "tls_probe", "tls_probe.dol")


class DolphinTlsTests(unittest.TestCase):
    def test_probe(self):
        if not os.environ.get("HC_DOLPHIN"):
            self.skipTest("set HC_DOLPHIN=1 to run Dolphin tests")
        import dolphin_run
        if dolphin_run.find_dolphin() is None:
            self.skipTest("Dolphin not found")
        if not os.path.exists(PROBE):
            self.skipTest("tls_probe.dol is not built")
        run = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "dolphin_run.py"), PROBE, "--seconds", "150",
                              "--expect", "TLSPROBE: RESULT PASS", "--fail", "TLSPROBE: RESULT FAIL"],
                             capture_output=True, text=True, timeout=400, cwd=ROOT)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
