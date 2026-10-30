"""Float color and matte contracts."""

import numpy as np


def to_linear(rgb):
    value = np.abs(rgb)
    return np.sign(rgb) * np.where(value <= 0.04045, value / 12.92,
                                  ((value + 0.055) / 1.055) ** 2.4)


def to_display(rgb):
    value = np.abs(rgb)
    return np.sign(rgb) * np.where(value <= 0.0031308, value * 12.92,
                                  1.055 * value ** (1 / 2.4) - 0.055)


def source_linear(source, cfg):
    rgb = source[..., :3].copy()
    if cfg["unpremultiply"]:
        alpha = source[..., 3:4]
        rgb = np.divide(rgb, alpha, out=np.zeros_like(rgb), where=alpha > 1e-6)
    return rgb if cfg["input_linear"] else to_linear(np.clip(rgb, 0, 1))


def prepare_rgb(source, cfg):
    rgb = source_linear(source, cfg)
    pivot = cfg["linear_contrast_pivot"]
    return cfg["linear_brightness"] * (pivot + cfg["linear_contrast"] * (rgb - pivot))


def undo_adjust(rgb, cfg):
    pivot = cfg["linear_contrast_pivot"]
    return (rgb / cfg["linear_brightness"] - pivot) / cfg["linear_contrast"] + pivot


def extract_hint(rgba, channel):
    if channel == 0:
        hint = rgba[..., 3]
    elif channel == 1:
        hint = rgba[..., 0]
    else:
        hint = rgba[..., :3] @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    return np.clip(hint, 0, 1).astype(np.float32)


def despill(rgb, source, cfg):
    channel = cfg["despill_color"]
    if channel == 0:
        # Screen hue from the plate, not the cleaned foreground.
        channel = 1 if np.mean(source[..., 1]) >= np.mean(source[..., 2]) else 2
    other = [c for c in range(3) if c != channel]
    pair = rgb[..., other]
    limit = pair.mean(axis=-1) if cfg["despill_method"] == 0 else pair.max(axis=-1)
    spill = np.maximum(rgb[..., channel] - limit, 0) * cfg["despill_amount"]
    result = rgb.copy()
    result[..., channel] -= spill
    result[..., other[0]] += spill * 0.5
    result[..., other[1]] += spill * 0.5
    return result


def compose(source, prediction, cfg):
    mode = cfg["output_mode"]
    if mode == 5:
        return source.copy()
    alpha = np.clip(prediction[..., 3:4], 0, 1)
    if mode == 2:
        return np.concatenate([np.repeat(alpha, 3, axis=-1), np.ones_like(alpha)], axis=-1)
    rgb = prediction[..., :3].copy()
    if cfg["keep_original"]:
        weight = np.clip((alpha - 0.95) / 0.05, 0, 1)
        rgb = rgb * (1 - weight) + source_linear(source, cfg) * weight
    if mode != 3:
        rgb = despill(rgb, source, cfg)
    if mode == 4:
        y, x = np.indices(alpha.shape[:2])
        check = np.where(((x // 32 + y // 32) % 2)[..., None], 0.35, 0.65)
        rgb = rgb * alpha + to_linear(check) * (1 - alpha)
    if not cfg["output_linear"]:
        rgb = to_display(rgb)
    if mode == 0:
        rgb *= alpha
    if mode in (3, 4):
        alpha = np.ones_like(alpha)
    return np.concatenate([rgb, alpha], axis=-1).astype(np.float32)
