"""Content-addressed inference results, independent of display controls."""

import hashlib
import json
import re
import shutil

from .protocol import VIEW_KEYS


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cache_key(paths, cfg, revision):
    digest = hashlib.sha256()
    payload = {k: v for k, v in cfg.items() if k not in VIEW_KEYS}
    digest.update(json.dumps([revision, payload], sort_keys=True).encode())
    for path in paths:
        digest.update(path.name.encode())
        digest.update(path.stat().st_size.to_bytes(8, "little"))
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def trim_cache(root, budget_bytes, keep):
    entries = []
    for path in root.iterdir():
        if path.is_symlink() or not path.is_dir() or not re.fullmatch(r"[a-f0-9]{64}", path.name):
            continue
        size = sum(p.stat().st_size for p in path.rglob("*") if p.is_file())
        entries.append((path.stat().st_mtime, path, size))
    total = sum(entry[2] for entry in entries)
    for _, path, size in sorted(entries):
        if total <= budget_bytes:
            break
        if path == keep:
            continue
        shutil.rmtree(path)
        total -= size
