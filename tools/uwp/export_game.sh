#!/bin/bash
# export_game.sh <project_dir> <output.pck> [godot_binary]
# Exports the game as a regular Godot pack (binary scenes, compiled scripts, desktop textures)
# with the "Xbox" preset. Editor plugins are switched off for the run: some of them drop their
# autoloads when unloaded (FrontendPlugin removes FrontendBridge), which breaks script compilation
# in a headless export. project.godot is restored afterwards.
set -euo pipefail
project=$1
out=$2
godot=${3:-$HOME/Downloads/godot-4.7.1/Godot.app/Contents/MacOS/Godot}

backup=$(mktemp)
cp "$project/project.godot" "$backup"
restore() { cp "$backup" "$project/project.godot"; rm -f "$backup"; }
trap restore EXIT

python3 -I - "$project/project.godot" <<'EOF'
import re, sys
path = sys.argv[1]
text = open(path, encoding="utf-8").read()
text = re.sub(r'^enabled=PackedStringArray\(.*\)$', 'enabled=PackedStringArray()', text, flags=re.M)
open(path, "w", encoding="utf-8").write(text)
EOF

log=$(mktemp)
"$godot" --headless --path "$project" --export-pack "Xbox" "$out" > "$log" 2>&1 || true
errors=$(grep -c 'SCRIPT ERROR' "$log" || true)
grep 'SCRIPT ERROR' -A1 "$log" | head -10 || true
rm -f "$log"
if [ ! -s "$out" ]; then
  echo "export failed: no pack written" >&2
  exit 1
fi
echo "exported $(du -h "$out" | cut -f1) to $out, script errors: $errors"
[ "$errors" = "0" ]
