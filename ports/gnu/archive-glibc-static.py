import argparse
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--ar", required=True)
    parser.add_argument("--ranlib", required=True)
    arguments = parser.parse_args()

    build = Path(arguments.build).resolve()
    stamps = sorted(build.glob("*/stamp.o"))
    if not stamps:
        raise SystemExit("glibc did not produce static object lists")

    objects = []
    for stamp in stamps:
        for name in stamp.read_text(encoding="ascii").split():
            relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise SystemExit(f"invalid object path in {stamp}: {name}")
            path = (build / relative).resolve()
            if build not in path.parents or not path.is_file():
                raise SystemExit(f"missing glibc object: {path}")
            objects.append(str(relative))

    if not objects or len(set(objects)) != len(objects):
        raise SystemExit("glibc object lists are empty or have duplicates")

    output = build / "libc.a"
    temporary = build / "libc.a.tmp"
    temporary.unlink(missing_ok=True)
    try:
        # Build one static archive from the completed object lists.
        subprocess.run([arguments.ar, "cr", str(temporary), *objects],
                       cwd=build, check=True)
        subprocess.run([arguments.ranlib, str(temporary)], check=True)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
