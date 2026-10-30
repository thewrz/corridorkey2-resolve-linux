"""Exercise real CUDA inference and cache using a small synthetic greenscreen plate."""

import argparse
import json
from pathlib import Path

import numpy as np

from worker.engine import Engine


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root
    job_dir = root / "build/smoke/job"
    job_dir.mkdir(parents=True, exist_ok=True)
    y, x = np.indices((256, 384))
    for i in range(2):
        mask = ((x - 192 - i * 2) / 65) ** 2 + ((y - 135) / 100) ** 2 < 1
        rgba = np.ones((256, 384, 4), np.float32)
        rgba[..., :3] = [0.04, 0.7, 0.08]
        rgba[mask, :3] = [0.65, 0.25, 0.17]
        rgba[..., 3] = mask
        rgba.tofile(job_dir / f"source_{i:06d}.rgba")
    engine = Engine(root / ".deps/CorridorKey2.0", root / ".cache/smoke")
    for hints in (0, 1):
        job = {"directory": str(job_dir), "width": 384, "height": 256,
               "frames": 2, "target": 1,
               "settings": {"hint_source": hints, "hint_frames": 1,
                            "auto_vram": False, "low_vram": True,
                            "hann_chunk": 2, "hann_stride": 1}}
        result = engine.process(job)
        image = np.fromfile(result["output"], "<f4").reshape(256, 384, 4)
        assert np.isfinite(image).all()
        assert image[..., 3].min() >= 0 and image[..., 3].max() <= 1
        assert image[..., 3].max() > image[..., 3].min() + 0.1
        np.save(root / f"build/smoke/hint-{hints}.npy", image)
        print(json.dumps({"hint": hints, **result,
                          "alpha_min": float(image[..., 3].min()),
                          "alpha_max": float(image[..., 3].max())}), flush=True)
        assert engine.process(job)["cached"]
    preview = {"directory": str(job_dir), "width": 384, "height": 256,
               "frames": 1, "target": 0,
               "settings": {"model_scale": 0.5, "hann_chunk": 1, "hann_stride": 1}}
    result = engine.process(preview)
    image = np.fromfile(result["output"], "<f4").reshape(256, 384, 4)
    assert np.isfinite(image).all()
    assert image[..., 3].max() - image[..., 3].min() > 0.5
    print(json.dumps({"automatic_half_resolution_preview": True, **result}), flush=True)


if __name__ == "__main__":
    main()
