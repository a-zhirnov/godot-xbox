#!/usr/bin/env python3
"""Pack an imported Godot project (sources + .godot/imported) into a PCK that runs as-is.

Usage: pack_source.py <project_dir> <output.pck>

Run `godot --headless --path <project_dir> --import` first. Text scenes and .gd scripts are
loaded directly by the engine, so no export templates are needed. Left out: VCS and editor
caches, Android-only addons, and original assets that have an imported version (.import).
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SKIP_DIRS = {".git", ".godot/editor", ".godot/shader_cache", "addons/AynThor/bin", "android"}
SKIP_FILES = {"gdre_export.log", "export_presets.cfg", ".gitignore", ".gitattributes"}
ANDROID_EXTENSIONS = ("res://addons/AynThor/AynThor.gdextension",)
# .godot folders holding earlier import results (the recovery's own cache, the decrypted APK).
FALLBACK_IMPORT_DIRS = [
    os.path.expanduser("~/Downloads/backups/socket-game-godot-cache-20261023"),
    os.path.expanduser("~/Downloads/socket_0.3.2_out/godot_project/.godot"),
]


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, out = os.path.abspath(sys.argv[1]), sys.argv[2]
    stage = tempfile.mkdtemp(prefix="socket-pck-")
    kept = 0
    for root, dirs, names in os.walk(src):
        rel_root = os.path.relpath(root, src).replace(os.sep, "/")
        rel_root = "" if rel_root == "." else rel_root
        dirs[:] = [d for d in dirs if (f"{rel_root}/{d}" if rel_root else d) not in SKIP_DIRS]
        for name in names:
            rel = f"{rel_root}/{name}" if rel_root else name
            if name in SKIP_FILES:
                continue
            # The engine loads the imported copy; the original source asset isn't needed.
            if os.path.exists(os.path.join(root, name + ".import")):
                continue
            # Xbox GPUs take the desktop (BC) copy; drop the mobile one when both were imported.
            if name.endswith((".etc2.ctex", ".astc.ctex")):
                stem = name.rsplit(".", 2)[0]
                if any(os.path.exists(os.path.join(root, stem + alt)) for alt in (".s3tc.ctex", ".bptc.ctex")):
                    continue
            dst = os.path.join(stage, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if rel == ".godot/extension_list.cfg":
                lines = open(os.path.join(root, name), encoding="utf-8").read().splitlines()
                open(dst, "w", encoding="utf-8").write("\n".join(l for l in lines if l.strip() not in ANDROID_EXTENSIONS) + "\n")
            else:
                os.link(os.path.join(root, name), dst) if os.stat(root).st_dev == os.stat(stage).st_dev else open(dst, "wb").write(open(os.path.join(root, name), "rb").read())
            kept += 1
    # Imports that can't be redone from the recovered sources (e.g. FBX models, whose sources
    # GDRE can't restore) still have their original output: take it from the fallback caches.
    restored = 0
    for root, _, names in os.walk(src):
        for name in names:
            if not name.endswith(".import"):
                continue
            lines = [l for l in open(os.path.join(root, name), encoding="utf-8", errors="replace")
                     if l.startswith(("path=", "path.")) and "res://.godot/imported/" in l]
            has_desktop = any(".s3tc.ctex" in l or ".bptc.ctex" in l for l in lines)
            for line in lines:
                if has_desktop and (".etc2.ctex" in line or ".astc.ctex" in line):
                    continue
                rel = line.split('"')[1].replace("res://", "")
                dst = os.path.join(stage, rel)
                if os.path.exists(dst):
                    continue
                for fallback in FALLBACK_IMPORT_DIRS:
                    candidate = os.path.join(fallback, rel.replace(".godot/", "", 1))
                    if os.path.exists(candidate):
                        os.makedirs(os.path.dirname(dst), exist_ok=True)
                        open(dst, "wb").write(open(candidate, "rb").read())
                        restored += 1
                        break
    print(f"staged {kept} files, restored {restored} imported files from fallback caches")
    return subprocess.call([sys.executable, "-I", os.path.join(HERE, "pack_game.py"), stage, out])


if __name__ == "__main__":
    sys.exit(main())
