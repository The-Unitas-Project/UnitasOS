"""Replace size expressions that the x86-64 cross assembler rejects."""

from pathlib import Path
import sys


source_root = Path(sys.argv[1])
replacements = {
    "sysdeps/unix/sysv/linux/errlist-compat.h": ("NUMBERERR * (ULONG_WIDTH / UCHAR_WIDTH)", 2),
    "sysdeps/generic/siglist-compat-def.h": ("NUMBERSIG * (ULONG_WIDTH / UCHAR_WIDTH)", 3),
}

for relative_path, (expression, count) in replacements.items():
    path = source_root / relative_path
    source = path.read_text()
    if source.count(expression) != count:
        raise SystemExit(f"Unexpected expression count in {path}")
    path.write_text(source.replace(expression, expression.split(" * ")[0] + " * 8"))
