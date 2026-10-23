#!/usr/bin/env python3
"""Pack a directory of already-imported Godot project files into an unencrypted PCK (format v3).

Usage: pack_game.py <project_dir> <output.pck> [godot_major godot_minor godot_patch]

The directory is expected to hold the contents of an exported project (project.binary,
.godot/imported, *.gdc scripts, ...). Android-only launcher leftovers are skipped.
"""

import hashlib
import os
import struct
import sys

PACK_HEADER_MAGIC = 0x43504447
PACK_FORMAT_VERSION = 3
PACK_REL_FILEBASE = 1 << 1
SKIP = {"_cl_", "assets.sparsepck", "dexopt"}
ALIGN = 16


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, out = sys.argv[1], sys.argv[2]
    ver = [int(v) for v in sys.argv[3:6]] if len(sys.argv) >= 6 else [4, 7, 1]

    files = []
    for root, dirs, names in os.walk(src):
        if os.path.relpath(root, src) == ".":
            dirs[:] = [d for d in dirs if d not in SKIP]
            names = [n for n in names if n not in SKIP]
        for name in names:
            full = os.path.join(root, name)
            rel = os.path.normpath(os.path.relpath(full, src)).replace(os.sep, "/")
            files.append((rel, full))
    files.sort()

    entries = []
    with open(out, "wb") as f:
        f.write(struct.pack("<IIIIII", PACK_HEADER_MAGIC, PACK_FORMAT_VERSION, ver[0], ver[1], ver[2], PACK_REL_FILEBASE))
        f.write(struct.pack("<QQ", 0, 0))  # file base, directory offset (patched below)
        f.write(b"\0" * 64)  # 16 reserved uint32
        f.write(b"\0" * ((-f.tell()) % ALIGN))
        file_base = f.tell()

        for rel, full in files:
            with open(full, "rb") as src_file:
                data = src_file.read()
            entries.append((rel, f.tell() - file_base, len(data), hashlib.md5(data).digest()))
            f.write(data)
            f.write(b"\0" * ((-f.tell()) % ALIGN))

        dir_offset = f.tell()
        f.write(struct.pack("<I", len(entries)))
        for rel, ofs, size, md5 in entries:
            raw = rel.encode("utf-8")
            padded = len(raw) + ((-len(raw)) % 4)
            f.write(struct.pack("<I", padded))
            f.write(raw + b"\0" * (padded - len(raw)))
            f.write(struct.pack("<QQ", ofs, size))
            f.write(md5)
            f.write(struct.pack("<I", 0))

        f.seek(24)
        f.write(struct.pack("<QQ", file_base, dir_offset))

    print(f"packed {len(entries)} files -> {out} ({os.path.getsize(out) / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
