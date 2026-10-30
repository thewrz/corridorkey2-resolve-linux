"""Disk-backed inference jobs."""

import contextlib
import fcntl
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("OPENCV_IO_ENABLE_OPENEXR", "1")
import cv2
import numpy as np

from .cache import cache_key, file_digest, trim_cache
from .color import compose, extract_hint, prepare_rgb, undo_adjust
from .progress import InferenceProgress, Progress
from .protocol import VIEW_KEYS, validate_job

REVISION = "bridge-2-torch2.13:485f5e9ab759ead6138cc990776184b59d6ea971:cf44a8db3f024bb49fd59daa408848ccf05f7477"


def read_rgba(path, width, height):
    if path.is_symlink() or path.stat().st_size != width * height * 16:
        raise ValueError(f"Invalid frame size: {path.name}")
    frame = np.fromfile(path, dtype="<f4").reshape(height, width, 4)
    if not np.isfinite(frame).all():
        raise ValueError(f"Non-finite pixels in {path.name}")
    return frame


def write_exr(path, pixels):
    if not cv2.imwrite(str(path), pixels,
                       [cv2.IMWRITE_EXR_TYPE, cv2.IMWRITE_EXR_TYPE_FLOAT]):
        raise OSError(f"Cannot write EXR: {path.name}")


def scaled_size(job, cfg):
    return tuple(max(1, round(job[key] * cfg["model_scale"])) for key in ("width", "height"))


def resize(pixels, size):
    if pixels.shape[:2] == (size[1], size[0]):
        return pixels
    return cv2.resize(pixels, size, interpolation=cv2.INTER_LANCZOS4)


class Engine:
    def __init__(self, model_root, cache_root, predictor=None):
        self.model_root = Path(model_root).resolve()
        self.cache_root = Path(cache_root).resolve() / "predictions-v1"
        self.cache_root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.predictor = predictor or self.predict
        self.model = None
        self.progress = Progress()

    def input_paths(self, root, job, cfg):
        paths = [root / f"source_{i:06d}.rgba" for i in range(job["frames"])]
        if cfg["hint_source"] == 2:
            count = 1 if cfg["hint_frames"] else job["frames"]
            paths += [root / f"hint_{i:06d}.rgba" for i in range(count)]
        for path in paths:
            if path.is_symlink() or path.stat().st_size != job["width"] * job["height"] * 16:
                raise ValueError(f"Invalid frame size: {path.name}")
        return paths

    def cache_valid(self, destination, job):
        marker = destination / "complete.json"
        if not marker.is_file():
            return False
        try:
            info = json.loads(marker.read_text())
            expected = {k: job[k] for k in ("width", "height", "frames")}
            if not isinstance(info, dict) or info.get("shape") != expected:
                return False
            if not isinstance(info.get("digests"), list) or len(info["digests"]) != job["frames"]:
                return False
            size = job["width"] * job["height"] * 16
            for i in range(job["frames"]):
                path = destination / f"{i:06d}.rgba"
                if path.stat().st_size != size or file_digest(path) != info["digests"][i]:
                    return False
            return True
        except (OSError, ValueError, KeyError, IndexError):
            return False

    def materialize(self, root, job, cfg, work):
        source_dir, hint_dir = work / "plate", work / "hint"
        source_dir.mkdir()
        hint_dir.mkdir()
        size = scaled_size(job, cfg)
        for i in range(job["frames"]):
            frame = read_rgba(root / f"source_{i:06d}.rgba", job["width"], job["height"])
            rgb = resize(prepare_rgb(frame, cfg), size)
            write_exr(source_dir / f"{i:06d}.exr", rgb[..., ::-1])
            self.progress(.08 + .12 * (i + 1) / job["frames"],
                          f"Preparing frame {i + 1} / {job['frames']}")
            if not cfg["hint_source"] or (cfg["hint_frames"] and i > 0):
                continue
            hint = frame if cfg["hint_source"] == 1 else read_rgba(
                root / f"hint_{i:06d}.rgba", job["width"], job["height"])
            matte = resize(extract_hint(hint, cfg["hint_channel"]), size).clip(0, 1)
            write_exr(hint_dir / f"{i:06d}.exr", matte)
        return source_dir, hint_dir if cfg["hint_source"] else None

    def process(self, job, progress=None):
        started = time.monotonic()
        self.progress = Progress(progress)
        self.progress(0, "Checking shot cache")
        root, cfg = validate_job(job)
        paths = self.input_paths(root, job, cfg)
        # Shape must be part of identity even when the byte count is identical.
        identity = f"{REVISION}:{self.model_root}:{job['width']}x{job['height']}"
        destination = self.cache_root / cache_key(paths, cfg, identity)
        with (self.cache_root / ".lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            # A killed worker cannot run TemporaryDirectory cleanup. The lock
            # proves no other worker is using a staging directory in this cache.
            for path in self.cache_root.iterdir():
                if (re.fullmatch(r"work-[a-z0-9_]{8}", path.name)
                        and path.is_dir() and not path.is_symlink()):
                    shutil.rmtree(path)
            cached = self.cache_valid(destination, job)
            if not cached:
                self.run_job(root, job, cfg, destination)
            self.progress(.9, "Preparing playback cache")
            view = self.prepare_view(root, job, cfg, destination)
            output = root / "output.rgba"
            shutil.copyfile(view / f"{job['target']:06d}.rgba", output)
            os.utime(destination, None)
            trim_cache(self.cache_root, int(cfg["cache_gb"] * 2**30), destination)
        self.progress(1, f"Ready — {job['frames']} frames cached")
        return {"output": str(output), "directory": str(destination), "cached": cached,
                "view_directory": str(view), "manifest": str(view / "complete.json"),
                "frames": job["frames"], "seconds": round(time.monotonic() - started, 2)}

    def prepare_view(self, root, job, cfg, predictions):
        payload = {key: cfg[key] for key in sorted(VIEW_KEYS - {"cache_gb"})}
        key = hashlib.sha256(json.dumps(["view-1", payload], sort_keys=True).encode()).hexdigest()
        destination = predictions / "views" / key
        if self.cache_valid(destination, job):
            return destination
        with tempfile.TemporaryDirectory(prefix="work-", dir=self.cache_root) as directory:
            result = Path(directory) / "view"
            result.mkdir()
            digests = []
            for i in range(job["frames"]):
                prediction = read_rgba(predictions / f"{i:06d}.rgba", job["width"], job["height"])
                source = read_rgba(root / f"source_{i:06d}.rgba", job["width"], job["height"])
                path = result / f"{i:06d}.rgba"
                compose(source, prediction, cfg).astype("<f4").tofile(path)
                digests.append(file_digest(path))
                self.progress(.9 + .09 * (i + 1) / job["frames"],
                              f"Caching frame {i + 1} / {job['frames']}")
            metadata = {"shape": {key: job[key] for key in ("width", "height", "frames")},
                        "digests": digests}
            (result / "complete.json").write_text(json.dumps(metadata))
            destination.parent.mkdir(exist_ok=True)
            if destination.exists():
                shutil.rmtree(destination)
            result.rename(destination)
        return destination

    def run_job(self, root, job, cfg, destination):
        estimated = job["width"] * job["height"] * job["frames"] * 64
        if shutil.disk_usage(self.cache_root).free < estimated + 512 * 2**20:
            raise OSError("Not enough cache disk space; shorten the shot or change Cache Folder")
        with tempfile.TemporaryDirectory(prefix="work-", dir=self.cache_root) as directory:
            work = Path(directory)
            source, hint = self.materialize(root, job, cfg, work)
            result = work / "result"
            result.mkdir()
            # Upstream's optimized EXR renderer rejects model_scale != 1. Scale
            # the actual inputs here, then restore predictions with Lanczos.
            self.predictor(source, hint, result, cfg | {"model_scale": 1.0})
            size = scaled_size(job, cfg)
            for i in range(job["frames"]):
                path = result / f"{i:06d}.rgba"
                prediction = resize(read_rgba(path, *size), (job["width"], job["height"]))
                prediction[..., 3] = prediction[..., 3].clip(0, 1)
                prediction[..., :3] = undo_adjust(prediction[..., :3], cfg)
                prediction.astype("<f4").tofile(path)
                self.progress(.85 + .04 * (i + 1) / job["frames"], "Validating neural predictions")
            metadata = {"shape": {k: job[k] for k in ("width", "height", "frames")},
                        "digests": [file_digest(result / f"{i:06d}.rgba")
                                    for i in range(job["frames"])]}
            (result / "complete.json").write_text(json.dumps(metadata))
            if destination.exists():
                shutil.rmtree(destination)
            result.rename(destination)

    def load_model(self):
        if self.model is not None:
            return
        sys.path.insert(0, str(self.model_root))
        from corridorkey.runtime import RuntimePaths, load_model

        from .compat import preserve_normalization_dtypes
        self.model = preserve_normalization_dtypes(
            load_model(RuntimePaths.from_package(self.model_root)))

    def predict(self, source_dir, hint_dir, destination, cfg):
        self.progress(.20, "Loading CorridorKey model")
        self.load_model()
        import torch
        from corridorkey.runtime import InferenceConfig, run_inference, select_vram_profile
        from corridorkey.video_io import WriterConfig

        config = InferenceConfig()
        for key in vars(config).keys() & cfg.keys():
            setattr(config, key, cfg[key])
        config.frame_dir_linear = True
        config.frame_dir_fps = cfg["fps"]
        config.linear_brightness = config.linear_contrast = 1.0  # already applied
        config.output_format, config.exr_dtype = "exr", "float"
        config.outputs = {"cutout"}
        config.despill_strength = 0.0  # output controls reuse raw neural predictions
        config.temp_dir = destination.parent / "features"
        if cfg["auto_vram"] and cfg["memory_gb"] > 0:
            frame = cv2.imread(str(next(source_dir.glob("*.exr"))), cv2.IMREAD_UNCHANGED)
            shape = tuple(int(np.ceil(v * cfg["model_scale"] / 128)) * 128
                          for v in frame.shape[:2])
            free = (torch.cuda.mem_get_info()[0] + torch.cuda.memory_reserved()) / 2**30
            chunk, stride, low, _ = select_vram_profile(
                shape, len(list(source_dir.glob("*.exr"))), min(free, cfg["memory_gb"]))
            config.hann_chunk, config.hann_stride, config.low_vram = chunk, stride, low
            config.auto_vram = False
        first = str(hint_dir / "000000.exr") if hint_dir and cfg["hint_frames"] else None
        full = str(hint_dir) if hint_dir and not cfg["hint_frames"] else None
        progress = InferenceProgress(sys.stdout, self.progress, len(list(source_dir.glob("*.exr"))))
        hook = self.model.stage2.register_forward_hook(
            lambda module, inputs, output: progress.frames_done(int(inputs[0].shape[1])))
        try:
            with contextlib.redirect_stdout(progress):
                run_inference(self.model, str(source_dir), str(destination / "exr"), config,
                              WriterConfig(), initial_hint_path=first, hint_video_path=full)
        finally:
            hook.remove()
        paths = sorted((destination / "exr").glob("plate_cutout_exr/*.exr"))
        if len(paths) != len(list(source_dir.glob("*.exr"))):
            raise RuntimeError("CorridorKey returned an incomplete shot")
        for i, path in enumerate(paths):
            bgra = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
            if bgra is None or bgra.ndim != 3 or bgra.shape[2] != 4:
                raise RuntimeError("CorridorKey returned an invalid RGBA image")
            bgra[..., [2, 1, 0, 3]].astype("<f4").tofile(destination / f"{i:06d}.rgba")
        shutil.rmtree(destination / "exr")
