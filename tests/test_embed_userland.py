import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EMBEDDER = ROOT / "tools/embed-userland.py"


class EmbedUserlandTests(unittest.TestCase):
    def test_aliases_share_one_embedded_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "init-image"
            source.write_bytes(b"\x7fELF" + bytes(range(64)))
            (root / "systemd").symlink_to(source)
            (root / "init").symlink_to(source)
            output = root / "userland.S"
            subprocess.run(
                [
                    "python3", str(EMBEDDER), "--output", str(output),
                    "--hello", str(source), "--image", str(root / "systemd"),
                    "--image", str(root / "init"),
                ],
                check=True,
            )
            assembly = output.read_text()
            self.assertIn('.asciz "systemd"', assembly)
            self.assertIn('.asciz "init"', assembly)
            self.assertEqual(assembly.count(".incbin "), 2)
            self.assertIn(".quad 2", assembly)

    def test_non_elf_program_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            hello = root / "hello"
            bad_program = root / "systemd"
            hello.write_bytes(b"\x7fELF" + b"hello")
            bad_program.write_bytes(b"not an ELF")
            result = subprocess.run(
                [
                    "python3", str(EMBEDDER), "--output", str(root / "out.S"),
                    "--hello", str(hello), "--image", str(bad_program),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not an ELF executable", result.stderr)


if __name__ == "__main__":
    unittest.main()
