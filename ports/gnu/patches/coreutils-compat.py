from pathlib import Path
import sys


source_root = Path(sys.argv[1])
header = source_root / "lib/mcel.h"
source = header.read_text(encoding="utf-8")
include = "#include <uchar.h>\n"
replacement = include + "#include <wchar.h>\n"

if "#include <wchar.h>" not in source:
    if include not in source:
        raise SystemExit(f"cannot patch {header}")
    header.write_text(source.replace(include, replacement, 1), encoding="utf-8")
