"""Builds and runs the priiloader app's host tests (tests/host/priiloader).

src/priiloader/source/hacks.cpp, nfo.cpp, password_file.cpp and settings.cpp are compiled unchanged for the PC,
against a simulated NAND (ISFS) and a heap that guards every block; mem2_manager.c is compiled as C for a
32 bit target (it casts pointers to u32), and src/Shared/IOSScan.hpp and playlog_id.h are included as they are.
Skipped when no clang++ is found. hacks.cpp, settings.cpp and mem2_manager.c can be swapped for another copy
(HC_PRIILOADER_HACKS, HC_PRIILOADER_SETTINGS, HC_PRIILOADER_MEM2 = path, e.g. from git show HEAD:...) to
check that the tests fail on the code they were written against. HC_ASAN=1 adds AddressSanitizer
(not to the 32 bit mem2 test). The tests that read files of the repository (hacks_hash.ini and
hacksh_s.ini in test_hacks, docs/settings.ini in test_settings) find it through HC_REPO_ROOT, which this sets.
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


def find_clang_c():
    """clang (the C driver) next to clang++, or on PATH."""
    cxx = find_clang()
    if cxx:
        base = os.path.join(os.path.dirname(cxx), "clang" + (".exe" if os.name == "nt" else ""))
        if os.path.exists(base):
            return base
    return shutil.which("clang")


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
            env = dict(os.environ)
            env["HC_REPO_ROOT"] = ROOT.replace("\\", "/")
            if os.name == "nt":
                # AddressSanitizer's runtime DLL sits in LLVM's own folders
                llvm = os.path.dirname(cxx) if os.path.dirname(cxx) else ""
                lib = os.path.join(os.path.dirname(llvm), "lib", "clang")
                extra = [llvm] + ([os.path.join(lib, v, "lib", "windows") for v in os.listdir(lib)] if os.path.isdir(lib) else [])
                env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
            try:
                run = subprocess.run([exe], capture_output=True, text=True, timeout=300, cwd=tmp, env=env)
            except subprocess.TimeoutExpired as e:
                self.fail("%s hung: %s" % (test, e.stdout))
            out = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, out)
            self.assertNotIn("FAIL", out)

    def build_and_run_c32(self, test, sources):
        """Builds a C test for a 32 bit target (the Wii's pointer size) and runs it."""
        cc = find_clang_c()
        if cc is None:
            self.skipTest("clang not found")
        with tempfile.TemporaryDirectory() as tmp:
            probe = os.path.join(tmp, "probe.c")
            with open(probe, "wb") as f:
                f.write(b"int main(void) { return 0; }")
            if subprocess.run([cc, "-m32", probe, "-o", os.path.join(tmp, "probe")],
                              capture_output=True, cwd=tmp).returncode != 0:
                self.skipTest("clang cannot build 32 bit programs here")
            exe = os.path.join(tmp, test + (".exe" if os.name == "nt" else ""))
            cmd = [cc, "-m32", "-std=gnu11", "-w", "-g", "-I", os.path.join(HOST, "stubs"), "-I", INC,
                   os.path.join(HOST, test + ".c")] + sources + ["-o", exe]
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

    def test_settings(self):
        settings = os.environ.get("HC_PRIILOADER_SETTINGS", os.path.join(SRC, "settings.cpp"))
        self.build_and_run("test_settings", [os.path.join(HOST, "sim.cpp"), settings])

    def test_mem2_manager(self):
        mem2 = os.environ.get("HC_PRIILOADER_MEM2", os.path.join(SRC, "mem2_manager.c"))
        self.build_and_run_c32("test_mem2", [mem2])

    def test_ios_scan(self):
        self.build_and_run("test_iosscan", [])

    def test_playlog_id(self):
        self.build_and_run("test_playlog_id", [])


if __name__ == "__main__":
    unittest.main()
