#!/usr/bin/env bash
set -euo pipefail
plugins="${XDG_DATA_HOME:-$HOME/.local/share}/OFX/Plugins"
export OFX_PLUGIN_PATH="$plugins${OFX_PLUGIN_PATH:+:$OFX_PLUGIN_PATH}"
exec /opt/resolve/bin/resolve "$@"
