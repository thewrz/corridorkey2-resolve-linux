#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
revision=485f5e9ab759ead6138cc990776184b59d6ea971
weights=cf44a8db3f024bb49fd59daa408848ccf05f7477
mkdir -p .deps
if [[ ! -d .deps/CorridorKey2.0/.git ]]; then
    git clone https://github.com/nikopueringer/CorridorKey2.0.git .deps/CorridorKey2.0
    git -C .deps/CorridorKey2.0 checkout --detach "$revision"
fi
if [[ "$(git -C .deps/CorridorKey2.0 rev-parse HEAD)" != "$revision" ]]; then
    printf 'Upstream checkout differs from the tested revision; preserve it and use a clean checkout.\n' >&2
    exit 1
fi
uv sync --frozen
uv run python .deps/CorridorKey2.0/scripts/download_weights.py --corridorkey_revision "$weights"
bash scripts/verify.sh
bash scripts/install.sh
