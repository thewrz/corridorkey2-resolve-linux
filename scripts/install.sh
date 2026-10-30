#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$root/build/CorridorKey2.ofx.bundle"
target="${XDG_DATA_HOME:-$HOME/.local/share}/corridorkey-resolve"
test -f "$bundle/Contents/Linux-x86-64/CorridorKey2.ofx"
test -x "$root/.venv/bin/python"
stamp=$(date +%Y%m%d-%H%M%S)
release="$target/releases/$stamp"
mkdir -p "$release"
cp -a "$bundle" "$release/"
ln -sfn "$release/CorridorKey2.ofx.bundle" "$target/current"
plugin="${XDG_DATA_HOME:-$HOME/.local/share}/OFX/Plugins/CorridorKey2.ofx.bundle"
if [[ -e "$plugin" || -L "$plugin" ]]; then
    if [[ ! -L "$plugin" || "$(readlink "$plugin")" != "$target/current" ]]; then
        printf 'Refusing to replace an unrelated plugin at %s\n' "$plugin" >&2
        exit 1
    fi
else
    mkdir -p "$(dirname -- "$plugin")"
    ln -s "$target/current" "$plugin"
fi
"$root/.venv/bin/python" "$root/scripts/desktop.py"
printf 'Installed %s\nRestart Resolve to scan the plugin.\n' "$plugin"
