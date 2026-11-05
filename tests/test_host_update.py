"""Builds and runs the GitHub update logic's host test (tests/host/update).

src/priiloader/source/update_github.cpp and src/Shared/version.cpp are compiled unchanged for the PC, with the
sha256 and platform files of mbedTLS (src/third_party/mbedtls, Priiloader's config). It checks the releases JSON
reader, the tag format, the choice of release, the version rules (against the old inline ones) and the digest
check. Skipped when no clang is found. HC_ASAN=1 adds AddressSanitizer.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "tests", "host", "update")
MBED = os.path.join(ROOT, "src", "third_party", "mbedtls")
PRII = os.path.join(ROOT, "src", "priiloader")
STUBS = os.path.join(ROOT, "tests", "host", "http", "stubs")


def find(names, extra):
    for candidate in names + [shutil.which(n) for n in extra]:
        if candidate and (os.path.exists(candidate) or shutil.which(candidate)):
            return candidate
    return None


class HostUpdateTests(unittest.TestCase):
    def test_update_logic(self):
        cxx = find([os.environ.get("HOSTCXX")], ["clang++"]) or find([r"C:\Program Files\LLVM\bin\clang++.exe"], [])
        cc = find([os.environ.get("HOSTCC")], ["clang"]) or find([r"C:\Program Files\LLVM\bin\clang.exe"], [])
        if cxx is None or cc is None:
            self.skipTest("clang not found")
        asan = ["-fsanitize=address"] if os.environ.get("HC_ASAN") else []
        with tempfile.TemporaryDirectory() as tmp:
            objs = []
            for name in ("sha256.c", "platform_util.c"):
                obj = os.path.join(tmp, name + ".o")
                cmd = [cc, "-std=c11", "-O1", "-w", "-c", '-DMBEDTLS_CONFIG_FILE="priiloader_mbedtls_config.h"',
                       "-I", MBED, "-I", os.path.join(MBED, "include")] + asan + [os.path.join(MBED, "library", name), "-o", obj]
                build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                objs.append(obj)
            exe = os.path.join(tmp, "test_update" + (".exe" if os.name == "nt" else ""))
            cmd = [cxx, "-std=c++17", "-g", "-D_CRT_SECURE_NO_WARNINGS", '-DMBEDTLS_CONFIG_FILE="priiloader_mbedtls_config.h"',
                   "-I", STUBS, "-I", os.path.join(PRII, "include"), "-I", os.path.join(ROOT, "src", "Shared"),
                   "-I", MBED, "-I", os.path.join(MBED, "include"), "-I", os.path.join(ROOT, "tests", "host")] + asan
            cmd += [os.path.join(HOST, "test_update.cpp"), os.path.join(PRII, "source", "update_github.cpp"),
                    os.path.join(ROOT, "src", "Shared", "version.cpp")] + objs + ["-o", exe]
            build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            env = dict(os.environ)
            if os.name == "nt":
                llvm = os.path.dirname(shutil.which(cxx) or cxx)
                lib = os.path.join(os.path.dirname(llvm), "lib", "clang")
                extra = [llvm] + ([os.path.join(lib, v, "lib", "windows") for v in os.listdir(lib)] if os.path.isdir(lib) else [])
                env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
            run = subprocess.run([exe], capture_output=True, text=True, timeout=300, cwd=tmp, env=env)
            out = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, out)
            self.assertNotIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
