"""Audit installed release versions, including PyTorch's CUDA wheel variants."""

import json
import subprocess
import sys
import tempfile
from importlib.metadata import distributions
from pathlib import Path


def main():
    packages = []
    for package in distributions():
        name = package.metadata["Name"]
        if name == "utils3d":
            origin = json.loads(package.read_text("direct_url.json") or "{}")
            commit = origin.get("vcs_info", {}).get("commit_id")
            if commit != "83bfa5bda008803f6521ce9547f94023b8afeddd":
                raise RuntimeError("utils3d does not match the reviewed source pin")
            print(f"utils3d: verified source pin {commit}; no PyPI advisory metadata", flush=True)
            continue
        # pip-audit otherwise skips +cu126 packages entirely. Advisories apply
        # to the upstream release version independent of the CUDA wheel flavor.
        packages.append(f"{name}=={package.version.split('+')[0]}")
    with tempfile.TemporaryDirectory(prefix="corridorkey-audit-") as directory:
        path = Path(directory) / "requirements.txt"
        path.write_text("\n".join(sorted(packages)) + "\n")
        print(f"Auditing {len(packages)} installed release versions", flush=True)
        result = subprocess.run([sys.executable, "-m", "pip_audit", "--no-deps",
                                 "--disable-pip", "-r", str(path)], check=False)
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
