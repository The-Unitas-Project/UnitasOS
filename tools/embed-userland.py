import argparse
import hashlib
from pathlib import Path


def assembly_string(value):
    escaped = []
    for byte in value.encode("utf-8"):
        if byte in (ord("\\"), ord('"')):
            escaped.append("\\" + chr(byte))
        elif 0x20 <= byte <= 0x7e:
            escaped.append(chr(byte))
        else:
            escaped.append(f"\\{byte:03o}")
    return "".join(escaped)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--hello", required=True)
    parser.add_argument("--image", action="append", default=[])
    arguments = parser.parse_args()
    lines = [
        '.section .rodata',
        '.balign 16',
        '.global unitas_hello_elf_start',
        '.global unitas_hello_elf_end',
        'unitas_hello_elf_start:',
        f'.incbin "{assembly_string(str(Path(arguments.hello).resolve()))}"',
        'unitas_hello_elf_end:',
    ]
    records = []
    images = {}
    for index, image in enumerate(arguments.image):
        image_path = Path(image)
        name = image_path.name
        # Keep the command name from a symlink while sharing its target image.
        path = image_path.resolve()
        if not name or any(not (char.isascii() and
                                (char.isalnum() or char in "._+-[]"))
                           for char in name):
            raise SystemExit(f"invalid embedded program name: {name}")
        label = f"unitas_program_{index}_name"
        image_bytes = path.read_bytes()
        if image_bytes[:4] != b"\x7fELF":
            raise SystemExit(f"program is not an ELF executable: {path}")
        digest = hashlib.sha256(image_bytes).digest()
        key = (digest, image_bytes)
        if key not in images:
            images[key] = (
                f"unitas_image_{len(images)}_start",
                f"unitas_image_{len(images)}_end",
                path,
            )
        start, end, _ = images[key]
        records.append((label, name, start, end))
        lines.extend([
            f'{label}:',
            f'.asciz "{assembly_string(name)}"',
        ])

    # Store each image once. Keep one table entry for each command name.
    for start, end, path in images.values():
        lines.extend([
            '.balign 16',
            f'.global {start}',
            f'.global {end}',
            f'{start}:',
            f'.incbin "{assembly_string(str(path))}"',
            f'{end}:',
        ])

    lines.extend([
        '.balign 8',
        '.global unitas_program_table',
        'unitas_program_table:',
    ])
    for label, _, start, end in records:
        lines.append(f'.quad {label}, {start}, {end}')
    lines.extend([
        '.global unitas_program_count',
        'unitas_program_count:',
        f'.quad {len(records)}',
        '.section .note.GNU-stack,"",@progbits',
        '',
    ])
    Path(arguments.output).write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    main()
