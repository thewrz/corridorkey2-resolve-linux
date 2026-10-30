import copy

import numpy as np
import pytest

from worker import color, protocol


def test_settings_reject_invalid_memory_and_window_contracts():
    for change in ({"hann_chunk": 0}, {"hann_stride": 9, "hann_chunk": 8},
                   {"hint_quality": float("nan")}, {"model_scale": 0},
                   {"unknown": 1}, {"linear_brightness": 0}):
        with pytest.raises(ValueError):
            protocol.settings(change)


def test_settings_defaults_are_independent():
    first = protocol.settings({})
    first["hint_quality"] = 0.1
    assert protocol.settings({})["hint_quality"] == 0.95


def test_signed_hdr_color_round_trip():
    rgb = np.array([-0.2, 0.0, 0.003, 0.18, 1.0, 12.0], dtype=np.float32)
    np.testing.assert_allclose(color.to_linear(color.to_display(rgb)), rgb, atol=2e-6)


def test_linear_adjustment_inverse_and_nonmutation():
    source = np.array([[[2.0, -0.1, 0.3, 0.5]]], np.float32)
    saved = source.copy()
    cfg = protocol.settings({"input_linear": True, "unpremultiply": True,
                             "linear_brightness": 2.0, "linear_contrast": 1.5})
    adjusted = color.prepare_rgb(source, cfg)
    np.testing.assert_allclose(color.undo_adjust(adjusted, cfg), source[..., :3] * 2)
    np.testing.assert_array_equal(source, saved)


def test_hint_channels_are_coverage_not_color():
    rgba = np.array([[[0.2, 0.8, 0.1, 0.7], [0, 0, 0, 2]]], np.float32)
    np.testing.assert_allclose(color.extract_hint(rgba, 0), [[0.7, 1]])
    np.testing.assert_allclose(color.extract_hint(rgba, 1), [[0.2, 0]])
    np.testing.assert_allclose(color.extract_hint(rgba, 2)[0, 0], 0.6219, atol=1e-5)


@pytest.mark.parametrize("mode,expected", [
    (0, [1.0, 0.25, -0.1, 0.5]), (1, [2.0, 0.5, -0.2, 0.5]),
    (2, [0.5, 0.5, 0.5, 1.0]), (3, [2.0, 0.5, -0.2, 1.0]),
    (5, [0.1, 0.2, 0.3, 1.0]),
])
def test_output_modes_and_hdr(mode, expected):
    source = np.array([[[0.1, 0.2, 0.3, 1.0]]], np.float32)
    prediction = np.array([[[2.0, 0.5, -0.2, 0.5]]], np.float32)
    cfg = protocol.settings({"output_mode": mode, "output_linear": True,
                             "despill_amount": 0, "input_linear": True})
    np.testing.assert_allclose(color.compose(source, prediction, cfg)[0, 0], expected)


def test_despill_changes_rgb_without_changing_alpha_or_prediction():
    image = np.array([[[0.1, 0.9, 0.1, 0.4]]], np.float32)
    before = image.copy()
    cfg = protocol.settings({"output_mode": 1, "output_linear": True,
                             "despill_amount": 1, "despill_color": 1})
    result = color.compose(image, image, cfg)
    np.testing.assert_allclose(result, [[[0.5, 0.1, 0.5, 0.4]]], atol=1e-6)
    np.testing.assert_array_equal(image, before)


def test_opaque_preservation_uses_source_in_correct_color_space():
    src = np.array([[[0.5, 0.5, 0.5, 1]]], np.float32)
    prediction = np.array([[[1, 0, 0, 1]]], np.float32)
    cfg = protocol.settings({"keep_original": True, "despill_amount": 0})
    np.testing.assert_allclose(color.compose(src, prediction, cfg), src, atol=1e-6)


def test_cache_key_tracks_source_hint_and_inference_but_not_view(tmp_path):
    from worker.cache import cache_key

    src = tmp_path / "source_000000.rgba"
    src.write_bytes(b"source")
    hint = tmp_path / "hint_000000.rgba"
    hint.write_bytes(b"hint")
    cfg = protocol.settings({})
    key = cache_key([src, hint], cfg, "v1")
    view = copy.deepcopy(cfg)
    view["output_mode"] = 2
    assert cache_key([src, hint], view, "v1") == key
    cfg["hint_quality"] = 0.5
    assert cache_key([src, hint], cfg, "v1") != key
    cfg["hint_quality"] = 0.95
    hint.write_bytes(b"changed hint")
    assert cache_key([src, hint], cfg, "v1") != key
    assert cache_key([src], cfg, "v2") != cache_key([src], cfg, "v1")
