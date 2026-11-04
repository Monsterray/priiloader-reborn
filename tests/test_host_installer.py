"""Builds and runs the Installer's host tests (tests/host/installer).

src/Installer/source/installer.cpp and nand.cpp are compiled unchanged for the PC, against a
simulated NAND (ISFS, ES, SHA) with fault injection. Skipped when no clang++ is found.
The sources can be swapped for older ones (git show HEAD:...) with HC_INSTALLER_SRC, to
check that the tests fail on the code they were written against.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "tests", "host", "installer")
SRC = os.path.join(ROOT, "src", "Installer")


def find_clang():
    for candidate in (os.environ.get("HOSTCXX"), shutil.which("clang++"),
                      r"C:\Program Files\LLVM\bin\clang++.exe"):
        if candidate and os.path.exists(candidate) or (candidate and shutil.which(candidate)):
            return candidate
    return None


class HostInstallerTests(unittest.TestCase):
    def test_installer_nand_sequence(self):
        cxx = find_clang()
        if cxx is None:
            self.skipTest("clang++ not found")
        src_dir = os.environ.get("HC_INSTALLER_SRC", os.path.join(SRC, "source"))
        inc_dir = os.environ.get("HC_INSTALLER_INC", os.path.join(SRC, "include"))
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "installer_host.exe" if os.name == "nt" else "installer_host")
            cmd = [
                cxx, "-std=c++17", "-g", "-Wno-c99-designator", "-Wno-reorder-init-list",
                "-I", os.path.join(HOST, "stubs"), "-I", inc_dir,
                "-DHC_INSTALLER_CPP=\"%s\"" % os.path.join(src_dir, "installer.cpp").replace("\\", "/"),
                "-DHC_NAND_CPP=\"%s\"" % os.path.join(src_dir, "nand.cpp").replace("\\", "/"),
            ] + [os.path.join(HOST, n) for n in
                 ("test_installer.cpp", "sim_nand.cpp", "installer_unit.cpp", "nand_unit.cpp")] + ["-o", exe]
            build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            # twice: IOS that replaces a file on rename (Dolphin's), and one that refuses (-105)
            for no_replace in (False, True):
                env = dict(os.environ)
                env.pop("HC_RENAME_NO_REPLACE", None)
                if no_replace:
                    env["HC_RENAME_NO_REPLACE"] = "1"
                run = subprocess.run([exe], capture_output=True, text=True, timeout=300, cwd=tmp, env=env)
                out = run.stdout + run.stderr
                self.assertEqual(run.returncode, 0, "rename_no_replace=%s\n%s" % (no_replace, out))
                self.assertNotIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
