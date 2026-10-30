"""Private parent/child JSON-lines protocol. No network listener."""

import argparse
import contextlib
import json
import os
import sys
import traceback
from pathlib import Path

# Invoked by absolute file path from the installed OFX resources directory.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
os.environ.setdefault("OPENCV_IO_ENABLE_OPENEXR", "1")

from worker.engine import Engine


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-root", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path, required=True)
    args = parser.parse_args()
    engine = Engine(args.model_root, args.cache_root)
    protocol_out = sys.stdout

    def progress(fraction, message):
        print(json.dumps({"event": "progress", "fraction": fraction, "message": message},
                         allow_nan=False), file=protocol_out, flush=True)

    for line in sys.stdin:
        try:
            if len(line) > 65536:
                raise ValueError("Request is too large")
            job = json.loads(line)
            with contextlib.redirect_stdout(sys.stderr):
                result = engine.process(job, progress=progress)
            response = {"ok": True, **result}
        except Exception as error:  # noqa: BLE001 -- serialize failures at the process boundary
            traceback.print_exc(file=sys.stderr)
            response = {"ok": False, "error": str(error)[:2000]}
        print(json.dumps(response, allow_nan=False), flush=True)


if __name__ == "__main__":
    main()
