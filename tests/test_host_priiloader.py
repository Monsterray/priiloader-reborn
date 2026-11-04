"""Builds and runs the priiloader app's host tests (tests/host/priiloader).

src/priiloader/source/hacks.cpp, nfo.cpp and password_file.cpp are compiled unchanged for the PC,
against a simulated NAND (ISFS) and a heap that guards every block. Skipped when no clang++ is found.
hacks.cpp can be swapped for another copy (HC_PRIILOADER_HACKS=path, e.g. from git show HEAD:...) to
check that the tests fail on the code they were written against. HC_ASAN=1 adds AddressSanitizer.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "tests", "host", "priiloader")
SRC = os.path.join(ROOT, "src", "priiloader", "source")
INC = os.path.join(ROOT, "src", "priiloader", "include")
SHARED = os.path.join(ROOT, "src", "Shared")


def find_clang():
    for candidate in (os.environ.get("HOSTCXX"), shutil.which("clang++"),
                      r"C:\Program Files\LLVM\bin\clang++.exe"):
        if candidate and (os.path.exists(candidate) or shutil.which(candidate)):
            return candidate
    return None


class HostPriiloaderTests(unittest.TestCase):
    def build_and_run(self, test, sources):
        cxx = find_clang()
        if cxx is None:
            self.skipTest("clang++ not found")
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, test + (".exe" if os.name == "nt" else ""))
            cmd = [cxx, "-std=c++17", "-g", "-D_CRT_SECURE_NO_WARNINGS", "-Wno-return-type-c-linkage",
                   "-I", os.path.join(HOST, "stubs"), "-I", INC, "-I", SHARED]
            if os.environ.get("HC_ASAN"):
                cmd.append("-fsanitize=address")
            cmd += [os.path.join(HOST, test + ".cpp")] + sources + ["-o", exe]
            build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            try:
                run = subprocess.run([exe], capture_output=True, text=True, timeout=300, cwd=tmp)
            except subprocess.TimeoutExpired as e:
                self.fail("%s hung: %s" % (test, e.stdout))
            out = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, out)
            self.assertNotIn("FAIL", out)

    def test_hacks(self):
        hacks = os.environ.get("HC_PRIILOADER_HACKS", os.path.join(SRC, "hacks.cpp"))
        self.build_and_run("test_hacks", [os.path.join(HOST, "sim.cpp"), os.path.join(HOST, "support.cpp"), hacks])

    def test_nfo(self):
        self.build_and_run("test_nfo", [os.path.join(SRC, "nfo.cpp")])

    def test_password_file(self):
        self.build_and_run("test_password", [os.path.join(HOST, "sim.cpp"), os.path.join(SRC, "password_file.cpp")])


if __name__ == "__main__":
    unittest.main()
