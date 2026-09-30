import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATCHER = ROOT / "ports/gnu/patches/glibc-x86_64-gas-memmove.py"


def write_source(root, relative_path, data):
    path = root / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data)
    return path


class GlibcPatchTests(unittest.TestCase):
    def make_source(self, root):
        loads = "#if VEC_SIZE == 64\n# define LARGE_LOAD_SIZE (VEC_SIZE * 2)\n#else\n# define LARGE_LOAD_SIZE (VEC_SIZE * 4)\n#endif\n"
        prefetch = "#  define PREFETCH_ONE_SET(dir, base, offset) \\\n"
        prefetch += " ".join("PREFETCH ((%s)base)" % index for index in range(7))
        prefetch += "\n# else\n"
        memmove = "#define PREFETCHED_LOAD_SIZE (VEC_SIZE * 4)\n"
        memmove += loads
        memmove += "movl\t$(PAGE_SIZE / LARGE_LOAD_SIZE), %ecx\n"
        memmove += "movl\t$(PAGE_SIZE / LARGE_LOAD_SIZE), %ecx\n"
        memmove += prefetch
        start = "#ifdef PIC\n\tmov main@GOTPCREL(%rip), %RDI_LP\n#else\n\tmov $main, %RDI_LP\n#endif"
        write_source(root, "sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S", memmove)
        write_source(root, "sysdeps/x86_64/start.S", start)

    def test_patch_is_idempotent_and_rewrites_expected_macros(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_source(root)
            subprocess.run(["python3", str(PATCHER), str(root)], check=True)
            patched = (root / "sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S").read_text()
            self.assertIn("# define LARGE_LOAD_COUNT 32", patched)
            self.assertEqual(patched.count("movl\t$LARGE_LOAD_COUNT, %ecx"), 2)
            self.assertEqual(patched.count("PREFETCH (("), 7)
            self.assertEqual(patched.count("))base"), 7)
            first_result = patched
            subprocess.run(["python3", str(PATCHER), str(root)], check=True)
            self.assertEqual(
                (root / "sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S").read_text(),
                first_result,
            )
            self.assertEqual(
                (root / "sysdeps/x86_64/start.S").read_text(),
                "\tmov main@GOTPCREL(%rip), %RDI_LP",
            )

    def test_patch_rejects_unknown_assembly_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_source(root)
            path = root / "sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S"
            path.write_text(path.read_text().replace("VEC_SIZE == 64", "VEC_SIZE == 16"))
            result = subprocess.run(
                ["python3", str(PATCHER), str(root)], capture_output=True, text=True
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unexpected large-load macro", result.stderr)


if __name__ == "__main__":
    unittest.main()
