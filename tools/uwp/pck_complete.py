#!/usr/bin/env python3
"""Add to an exported PCK the imported resources whose source files are gone.

Usage: pck_complete.py <project_dir> <exported.pck> <output.pck>

A Godot export skips any import whose source is missing. The recovered Socket project has no FBX
sources (only their imported scenes), so the cartridge models drop out of the export and every
scene that uses them fails to load. This unpacks the export, adds each missing `<file>.import` and
its desktop imported file from <project_dir>/.godot/imported, and repacks with pack_game.py.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def unpack(pck: str, dest: str) -> set:
    names = set()
    with open(pck, "rb") as f:
        magic, version, _, _, _, flags = struct.unpack("<IIIIII", f.read(24))
        if magic != 0x43504447:
            raise SystemExit("not a pck")
        file_base, dir_offset = struct.unpack("<QQ", f.read(16))
        f.seek(dir_offset)
        count = struct.unpack("<I", f.read(4))[0]
        entries = []
        for _ in range(count):
            length = struct.unpack("<I", f.read(4))[0]
            path = f.read(length).rstrip(b"\0").decode("utf-8")
            offset, size = struct.unpack("<QQ", f.read(16))
            f.read(16 + 4)  # md5, flags
            entries.append((path, offset, size))
        for path, offset, size in entries:
            f.seek(file_base + offset)
            out = os.path.join(dest, path)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as o:
                o.write(f.read(size))
            names.add(path)
    return names


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    project, exported, output = sys.argv[1:4]
    stage = tempfile.mkdtemp(prefix="socket-pck-complete-")
    try:
        names = unpack(exported, stage)
        added = 0
        for root, dirs, files in os.walk(project):
            dirs[:] = [d for d in dirs if d not in (".git", ".godot")]
            for name in files:
                if not name.endswith(".import"):
                    continue
                rel = os.path.relpath(os.path.join(root, name), project).replace(os.sep, "/")
                if rel in names:
                    continue
                lines = [l for l in open(os.path.join(root, name), encoding="utf-8", errors="replace")
                         if l.startswith(("path=", "path.")) and "res://.godot/imported/" in l]
                targets = [l.split('"')[1].replace("res://", "") for l in lines]
                desktop = [t for t in targets if not t.endswith((".etc2.ctex", ".astc.ctex"))] or targets
                present = [t for t in desktop if os.path.exists(os.path.join(project, t))]
                if not present:
                    continue
                os.makedirs(os.path.dirname(os.path.join(stage, rel)), exist_ok=True)
                shutil.copy2(os.path.join(root, name), os.path.join(stage, rel))
                for t in present:
                    dst = os.path.join(stage, t)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copy2(os.path.join(project, t), dst)
                added += 1
                print("added", rel)
        print(f"{added} sourceless imports added")
        return subprocess.call([sys.executable, "-I", os.path.join(HERE, "pack_game.py"), stage, output])
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
