"""Install the standard OFX search path into the user's Resolve launcher."""

import os
import shutil
from pathlib import Path


def with_plugin_path(text, path):
    if any(char in str(path) for char in ('"', '\n', '\r', '`', '$', '%', '\\')):
        raise ValueError("Unsupported character in desktop plugin path")
    assignment = f'OFX_PLUGIN_PATH="{path}"'
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("Exec="):
            continue
        command = line[5:]
        if assignment in command:
            return text
        if "OFX_PLUGIN_PATH=" in command:
            raise ValueError("Launcher already sets OFX_PLUGIN_PATH; merge the paths manually")
        lines[index] = f"Exec=env {assignment} {command}"
        return "\n".join(lines) + "\n"
    raise ValueError("Resolve desktop entry has no Exec line")


def main():
    data = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    target = data / "applications/DaVinciResolve.desktop"
    original = target if target.exists() else Path("/usr/share/applications/DaVinciResolve.desktop")
    text = original.read_text()
    updated = with_plugin_path(text, data / "OFX/Plugins")
    target.parent.mkdir(parents=True, exist_ok=True)
    backup = target.with_suffix(".desktop.before-corridorkey")
    if target.exists() and not backup.exists():
        shutil.copy2(target, backup)
    temporary = target.with_suffix(".desktop.tmp")
    temporary.write_text(updated)
    temporary.replace(target)
    print(f"Resolve launcher configured: {target}")


if __name__ == "__main__":
    main()
