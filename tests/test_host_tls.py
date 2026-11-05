"""Builds and runs the TLS layer's host test (tests/host/tls).

mbedTLS (src/third_party/mbedtls, with Priiloader's config) and the TLS layer's pure files (tls_verify.c,
tls_roots.c) are compiled unchanged for the PC with clang. It checks the verify-flag mapping, that the embedded
roots parse and are valid, and that chains captured from api.github.com and objects.githubusercontent.com
verify against them. Skipped when no clang is found.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "tests", "host", "tls")
MBED = os.path.join(ROOT, "src", "third_party", "mbedtls")
TLS = os.path.join(ROOT, "src", "priiloader", "tls")


def find_clang():
    for candidate in (os.environ.get("HOSTCC"), shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if candidate and (os.path.exists(candidate) or shutil.which(candidate)):
            return candidate
    return None


class HostTlsTests(unittest.TestCase):
    def test_tls_layer(self):
        cc = find_clang()
        if cc is None:
            self.skipTest("clang not found")
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "test_tls" + (".exe" if os.name == "nt" else ""))
            sources = sorted(glob.glob(os.path.join(MBED, "library", "*.c")))
            sources += [os.path.join(HOST, "test_tls.c"), os.path.join(TLS, "source", "tls_verify.c"),
                        os.path.join(TLS, "source", "tls_roots.c")]
            cmd = [cc, "-std=c11", "-O1", "-w", "-D_CRT_SECURE_NO_WARNINGS",
                   '-DMBEDTLS_CONFIG_FILE="priiloader_mbedtls_config.h"',
                   "-I", MBED, "-I", os.path.join(MBED, "include"), "-I", os.path.join(TLS, "include"),
                   "-I", os.path.join(ROOT, "tests", "host")]
            if os.environ.get("HC_ASAN"):
                cmd.append("-fsanitize=address")
            cmd += sources + ["-o", exe]
            build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            env = dict(os.environ)
            if os.name == "nt":
                llvm = os.path.dirname(shutil.which(cc) or cc)
                lib = os.path.join(os.path.dirname(llvm), "lib", "clang")
                extra = [llvm] + ([os.path.join(lib, v, "lib", "windows") for v in os.listdir(lib)] if os.path.isdir(lib) else [])
                env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
            run = subprocess.run([exe, os.path.join(HOST, "chains")], capture_output=True, text=True, timeout=300, cwd=tmp, env=env)
            out = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, out)
            self.assertNotIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
