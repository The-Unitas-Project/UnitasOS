"""Flatten nested x86-64 size expressions for the cross assembler."""

from pathlib import Path
import sys


source_root = Path(sys.argv[1])
path = source_root / "sysdeps/x86_64/multiarch/memcmp-sse2.S"
source = path.read_text()
replacements = {
    "#  define SIZE_OFFSET\t(0)": "#  define SIZE_OFFSET\t0",
    "#   define SIZE_OFFSET\t(CHAR_PER_VEC * 2)": "#   define SIZE_OFFSET\t32",
    "# define CHAR_PER_VEC\t(VEC_SIZE / CHAR_SIZE)": (
        "# ifdef USE_AS_WMEMCMP\n"
        "#  define CHAR_PER_VEC 4\n"
        "# else\n"
        "#  define CHAR_PER_VEC 16\n"
        "# endif"
    ),
}
expected = {
    "#  define SIZE_OFFSET\t(0)": 2,
    "#   define SIZE_OFFSET\t(CHAR_PER_VEC * 2)": 1,
    "# define CHAR_PER_VEC\t(VEC_SIZE / CHAR_SIZE)": 1,
}
for expression, count in expected.items():
    if source.count(expression) != count:
        raise SystemExit(f"Unexpected expression count in {path}")
for expression, replacement in replacements.items():
    source = source.replace(expression, replacement)
path.write_text(source)
