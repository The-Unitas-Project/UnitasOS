import os
import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class GnuMakeTests(unittest.TestCase):
    def test_linux_build_uses_host_compiler_when_cc_is_set(self):
        make = shutil.which("gmake") or shutil.which("make")
        host_compiler = shutil.which("cc") or shutil.which("clang")
        if not make or not host_compiler:
            self.skipTest("GNU Make and a host C compiler are required")

        version = subprocess.run(
            [make, "--version"], capture_output=True, text=True
        )
        if version.returncode != 0 or "GNU Make" not in version.stdout:
            self.skipTest("GNU Make is required")

        environment = os.environ.copy()
        environment.pop("GNU_NATIVE_CC", None)
        result = subprocess.run(
            [
                make,
                "--no-print-directory",
                "--eval=print-probe-cc:;@printf '%s\\n' '$(GNU_NATIVE_CC)'",
                "-C",
                str(ROOT / "ports/gnu"),
                "HOST_OS=Linux",
                "CC=/unitas-target/bin/x86_64-linux-gnu-gcc",
                "print-probe-cc",
            ],
            capture_output=True,
            text=True,
            env=environment,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), host_compiler)


if __name__ == "__main__":
    unittest.main()
