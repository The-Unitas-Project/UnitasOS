import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "build/gnu-tools/bin/unitas-user-gcc"
SYSROOT = ROOT / "build/gnu-sysroot"


class GnuCompilerTests(unittest.TestCase):
    def test_compiler_builds_static_linux_executable(self):
        crt1 = SYSROOT / "usr/lib64/crt1.o"
        crti = SYSROOT / "usr/lib64/crti.o"
        crtn = SYSROOT / "usr/lib64/crtn.o"
        required = (COMPILER, crt1, crti, crtn, SYSROOT / "usr/lib64/libc.a")
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            self.skipTest("GNU compiler or sysroot is not built: " + ", ".join(missing))

        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "probe.c"
            output = work / "probe"
            source.write_text("int main(void) { return 0; }\n")
            command = [
                str(COMPILER),
                "-o",
                str(output),
                "-O2",
                "-g",
                "-mcmodel=large",
                "-fno-pie",
                "-fno-pic",
                "-static",
                "-no-pie",
                "-nostartfiles",
                "-Wl,-Ttext-segment=0x100000000",
                str(crt1),
                str(crti),
                str(source),
                "-lc",
                "-lgcc",
                str(crtn),
            ]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(
                result.returncode,
                0,
                "Compiler probe failed.\n"
                + " ".join(command)
                + "\n"
                + result.stdout
                + result.stderr,
            )

            image = output.read_bytes()
            self.assertGreaterEqual(len(image), 64)
            self.assertEqual(image[:4], b"\x7fELF")
            self.assertEqual(image[4], 2)
            self.assertEqual(image[5], 1)
            self.assertEqual(struct.unpack_from("<H", image, 16)[0], 2)
            self.assertEqual(struct.unpack_from("<H", image, 18)[0], 62)

            program_header_offset = struct.unpack_from("<Q", image, 32)[0]
            program_header_size = struct.unpack_from("<H", image, 54)[0]
            program_header_count = struct.unpack_from("<H", image, 56)[0]
            program_types = [
                struct.unpack_from("<I", image, program_header_offset + index * program_header_size)[0]
                for index in range(program_header_count)
            ]
            self.assertNotIn(3, program_types)


if __name__ == "__main__":
    unittest.main()
