"""Validated bridge settings."""

import math
from pathlib import Path

DEFAULTS = {
    "input_linear": False, "unpremultiply": False, "model_scale": 1.0,
    "hint_source": 0, "hint_channel": 0, "hint_frames": 0,
    "hint_quality": 0.95, "carry_hint_quality": 0.95, "carry_hint": True,
    "hann_chunk": 8, "hann_stride": 4, "auto_vram": True, "low_vram": True,
    "memory_gb": 0.0, "stage2_batch": 1,
    "low_vram_foundation_chunk": 4, "low_vram_enc_frame_chunk": 1,
    "low_vram_mlp_chunk_tokens": 65536, "low_vram_swin_window_batch": 16,
    "low_vram_full_attn_query_chunk_tokens": 2048,
    "linear_brightness": 1.0, "linear_contrast": 1.0, "linear_contrast_pivot": 0.18,
    "output_mode": 0, "output_linear": False, "keep_original": False,
    "despill_color": 0, "despill_method": 0, "despill_amount": 0.0,
    "fps": 24.0, "cache_gb": 20.0,
}
RANGES = {
    "model_scale": (0.125, 1), "hint_source": (0, 2), "hint_channel": (0, 2),
    "hint_frames": (0, 1), "hint_quality": (0, 1), "carry_hint_quality": (0, 1),
    "hann_chunk": (1, 80), "hann_stride": (1, 80), "memory_gb": (0, 128),
    "stage2_batch": (1, 16), "low_vram_foundation_chunk": (1, 32),
    "low_vram_enc_frame_chunk": (1, 16), "low_vram_mlp_chunk_tokens": (128, 1048576),
    "low_vram_swin_window_batch": (1, 256),
    "low_vram_full_attn_query_chunk_tokens": (128, 65536),
    "linear_brightness": (0.01, 100), "linear_contrast": (0.01, 100),
    "linear_contrast_pivot": (0, 10), "output_mode": (0, 5),
    "despill_color": (0, 2), "despill_method": (0, 1), "despill_amount": (0, 1),
    "fps": (0.001, 1000), "cache_gb": (0.1, 1000),
}
VIEW_KEYS = {"output_mode", "output_linear", "keep_original", "despill_color",
             "despill_method", "despill_amount", "cache_gb"}


def settings(values):
    if not isinstance(values, dict) or values.keys() - DEFAULTS.keys():
        raise ValueError("Unknown or malformed CorridorKey settings")
    result = DEFAULTS | values
    for key, value in result.items():
        default = DEFAULTS[key]
        if isinstance(default, bool):
            if not isinstance(value, bool):
                raise TypeError(f"{key} must be boolean")
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise TypeError(f"{key} must be numeric")
        if not math.isfinite(value) or not RANGES[key][0] <= value <= RANGES[key][1]:
            raise ValueError(f"{key} must be between {RANGES[key]}")
        if isinstance(default, int) and not isinstance(value, int):
            raise TypeError(f"{key} must be an integer")
    if result["hann_stride"] > result["hann_chunk"]:
        raise ValueError("Hann stride must not exceed its window size")
    return result


def validate_job(job):
    for key, low, high in (("width", 1, 16384), ("height", 1, 16384),
                           ("frames", 1, 10000), ("target", 0, 9999)):
        value = job.get(key)
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f"Invalid {key}")
    if job["target"] >= job["frames"]:
        raise ValueError("Target frame is outside shot")
    if job["width"] * job["height"] > 134217728:
        raise ValueError("Image exceeds 128 megapixels")
    root = Path(job["directory"]).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("Job directory is missing")
    return root, settings(job.get("settings", {}))
