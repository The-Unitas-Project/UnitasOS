"""Apply x86-64 assembly fixes for Unitas static programs."""

from pathlib import Path
import sys


source_root = Path(sys.argv[1])
path = source_root / "sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S"
source = path.read_text()
old_size = "#define PREFETCHED_LOAD_SIZE (VEC_SIZE * 4)"
new_size = "#define PREFETCHED_LOAD_SIZE VEC_SIZE * 4"
if source.count(old_size) == 1:
    source = source.replace(old_size, new_size)
elif source.count(new_size) != 1:
    raise SystemExit(f"Unexpected prefetch size macro in {path}")

old_loads = (
    "#if VEC_SIZE == 64\n"
    "# define LARGE_LOAD_SIZE (VEC_SIZE * 2)\n"
    "#else\n"
    "# define LARGE_LOAD_SIZE (VEC_SIZE * 4)\n"
    "#endif"
)
new_loads = (
    "#if VEC_SIZE == 64\n"
    "# define LARGE_LOAD_SIZE 128\n"
    "# define LARGE_LOAD_COUNT 32\n"
    "#else\n"
    "# if VEC_SIZE == 32\n"
    "#  define LARGE_LOAD_SIZE 128\n"
    "#  define LARGE_LOAD_COUNT 32\n"
    "# else\n"
    "#  define LARGE_LOAD_SIZE 64\n"
    "#  define LARGE_LOAD_COUNT 64\n"
    "# endif\n"
    "#endif"
)
if source.count(old_loads) == 1:
    source = source.replace(old_loads, new_loads)
elif source.count(new_loads) != 1:
    raise SystemExit(f"Unexpected large-load macro in {path}")

old_count = "movl\t$(PAGE_SIZE / LARGE_LOAD_SIZE), %ecx"
intermediate_count = "movl\t$(LARGE_LOAD_COUNT), %ecx"
new_count = "movl\t$LARGE_LOAD_COUNT, %ecx"
if source.count(old_count) == 2:
    source = source.replace(old_count, new_count)
elif source.count(intermediate_count) == 2:
    source = source.replace(intermediate_count, new_count)
elif source.count(new_count) != 2:
    raise SystemExit(f"Unexpected large-load count in {path}")

start = source.index("#  define PREFETCH_ONE_SET(dir, base, offset)")
end = source.index("# else", start)
macro = source[start:end]
expected = 7
if (macro.count("PREFETCH ((") == expected and
        macro.count("))base") == expected):
    pass
elif macro.count(")base)") == expected:
    macro = macro.replace(")base)", "))base")
elif (macro.count("PREFETCH ((") == 0 and
      macro.count("PREFETCH (") == expected and
      macro.count(")base") == expected):
    macro = macro.replace("PREFETCH (", "PREFETCH ((")
    macro = macro.replace(")base", "))base")
else:
    raise SystemExit(f"Unexpected prefetch macro form in {path}")
path.write_text(source[:start] + macro + source[end:])

start_path = source_root / "sysdeps/x86_64/start.S"
start_source = start_path.read_text()
old_start = (
    "#ifdef PIC\n"
    "\tmov main@GOTPCREL(%rip), %RDI_LP\n"
    "#else\n"
    "\tmov $main, %RDI_LP\n"
    "#endif"
)
new_start = "\tmov main@GOTPCREL(%rip), %RDI_LP"
if start_source.count(old_start) == 1:
    start_source = start_source.replace(old_start, new_start)
elif start_source.count(new_start) != 1:
    raise SystemExit(f"Unexpected x86-64 startup code in {start_path}")
start_path.write_text(start_source)
