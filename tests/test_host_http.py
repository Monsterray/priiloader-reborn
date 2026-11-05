"""Builds and runs the HTTP client's host test (tests/host/http).

src/priiloader/source/HTTP_Parser.cpp is compiled unchanged for the PC, against a fake network layer
(canned responses, an open-socket count) and a counting heap. Skipped when no clang++ is found.
HTTP_Parser.cpp can be swapped for another copy (HC_HTTP_PARSER=path, e.g. from git show HEAD:...) to
check that the tests fail on the code they were written against. HC_ASAN=1 adds AddressSanitizer.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "tests", "host", "http")
SRC = os.path.join(ROOT, "src", "priiloader", "source")
INC = os.path.join(ROOT, "src", "priiloader", "include")


def find_clang():
    for candidate in (os.environ.get("HOSTCXX"), shutil.which("clang++"),
                      r"C:\Program Files\LLVM\bin\clang++.exe"):
        if candidate and (os.path.exists(candidate) or shutil.which(candidate)):
            return candidate
    return None


class HostHttpTests(unittest.TestCase):
    def test_http_parser(self):
        cxx = find_clang()
        if cxx is None:
            self.skipTest("clang++ not found")
        parser = os.path.join(SRC, "HTTP_Parser.cpp")
        with tempfile.TemporaryDirectory() as tmp:
            override = os.environ.get("HC_HTTP_PARSER")
            if override:
                # the parser includes "../../Shared/gitrev.h" relative to itself: build the same layout
                os.makedirs(os.path.join(tmp, "a", "b"))
                os.makedirs(os.path.join(tmp, "Shared"))
                shutil.copy(os.path.join(ROOT, "src", "Shared", "gitrev.h"), os.path.join(tmp, "Shared"))
                parser = os.path.join(tmp, "a", "b", "HTTP_Parser.cpp")
                shutil.copy(override, parser)
            exe = os.path.join(tmp, "test_http" + (".exe" if os.name == "nt" else ""))
            cmd = [cxx, "-std=c++17", "-g", "-D_CRT_SECURE_NO_WARNINGS", "-Wno-return-type-c-linkage",
                   "-I", os.path.join(HOST, "stubs"), "-I", INC, "-I", os.path.join(ROOT, "tests", "host")]
            if os.environ.get("HC_ASAN"):
                cmd.append("-fsanitize=address")
            cmd += [os.path.join(HOST, "test_http.cpp"), parser, "-o", exe]
            build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            env = dict(os.environ)
            if os.name == "nt":
                # AddressSanitizer's runtime DLL sits in LLVM's own folders
                llvm = os.path.dirname(shutil.which(cxx) or cxx)
                lib = os.path.join(os.path.dirname(llvm), "lib", "clang")
                extra = [llvm] + ([os.path.join(lib, v, "lib", "windows") for v in os.listdir(lib)] if os.path.isdir(lib) else [])
                env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
            try:
                run = subprocess.run([exe], capture_output=True, text=True, timeout=300, cwd=tmp, env=env)
            except subprocess.TimeoutExpired as e:
                self.fail("test_http hung: %s" % e.stdout)
            out = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, out)
            self.assertNotIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
