from pathlib import Path

import numpy as np
import pytest

from worker import engine


def make_job(tmp_path, **settings):
    directory = tmp_path / "job"
    directory.mkdir()
    source = np.zeros((3, 4, 4), np.float32)
    source[..., 1] = 0.5
    source[..., 3] = 1
    for i in range(2):
        source.tofile(directory / f"source_{i:06d}.rgba")
    return {"directory": str(directory), "width": 4, "height": 3, "frames": 2,
            "target": 1, "settings": settings}


class Predict:
    """Substitute only the expensive model boundary, exercising real file/color I/O."""
    def __init__(self):
        self.calls = []

    def __call__(self, source_dir, hint_dir, destination, cfg):
        import cv2
        self.calls.append((source_dir, hint_dir, cfg.copy()))
        for i, path in enumerate(sorted(source_dir.glob("*.exr"))):
            bgr = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
            assert bgr.shape == (3, 4, 3)
            alpha = np.full((3, 4, 1), 0.5, np.float32)
            prediction = np.concatenate([bgr[..., ::-1], alpha], axis=-1)
            prediction.tofile(destination / f"{i:06d}.rgba")


def test_process_reuses_model_result_for_output_changes(tmp_path):
    job = make_job(tmp_path, input_linear=True, output_linear=True)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    first = service.process(job)
    result = np.fromfile(first["output"], np.float32).reshape(3, 4, 4)
    np.testing.assert_allclose(result[0, 0], [0, 0.25, 0, 0.5])
    assert not first["cached"]
    job["settings"]["output_mode"] = 2
    second = service.process(job)
    assert second["cached"]
    assert len(predictor.calls) == 1
    np.testing.assert_allclose(np.fromfile(second["output"], np.float32)[:4], [.5, .5, .5, 1])


def test_hint_materialization_uses_only_first_frame_when_requested(tmp_path):
    job = make_job(tmp_path, hint_source=1, hint_frames=1)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    service.process(job)
    # Predictor sees a directory containing exactly the first frame hint.
    assert predictor.calls[0][2]["hint_frames"] == 1
    assert predictor.calls[0][1] is not None


def test_truncated_input_never_reaches_model(tmp_path):
    job = make_job(tmp_path)
    (Path(job["directory"]) / "source_000001.rgba").write_bytes(b"bad")
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    with pytest.raises(ValueError, match="size"):
        service.process(job)
    assert not predictor.calls


def test_input_change_invalidates_cached_predictions(tmp_path):
    job = make_job(tmp_path)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    service.process(job)
    np.ones((3, 4, 4), np.float32).tofile(Path(job["directory"]) / "source_000001.rgba")
    assert not service.process(job)["cached"]
    assert len(predictor.calls) == 2


def test_corrupt_cached_output_is_recomputed(tmp_path):
    job = make_job(tmp_path)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    result = service.process(job)
    (Path(result["directory"]) / "000001.rgba").write_bytes(b"bad")
    assert not service.process(job)["cached"]
    assert len(predictor.calls) == 2


def test_same_size_finite_cache_corruption_is_recomputed(tmp_path):
    job = make_job(tmp_path)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    result = service.process(job)
    path = Path(result["directory"]) / "000001.rgba"
    pixels = np.fromfile(path, np.float32)
    pixels[0] = 123.0
    pixels.tofile(path)
    assert not service.process(job)["cached"]
    assert len(predictor.calls) == 2


@pytest.mark.parametrize("marker", ["[]", '{"shape": null}',
    '{"shape": {"width": 4, "height": 3, "frames": 2}, "digests": null}'])
def test_malformed_cache_metadata_is_recomputed(tmp_path, marker):
    job = make_job(tmp_path)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    result = service.process(job)
    (Path(result["directory"]) / "complete.json").write_text(marker)
    assert not service.process(job)["cached"]
    assert len(predictor.calls) == 2


def test_interrupted_work_directory_is_cleaned_before_next_job(tmp_path):
    job = make_job(tmp_path)
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=Predict())
    stale = service.cache_root / "work-stale123"
    stale.mkdir()
    (stale / "partial-features").write_bytes(b"interrupted by worker termination")
    other = service.cache_root / "user-notes"
    other.mkdir()
    service.process(job)
    assert not stale.exists()
    assert other.exists()


def test_scaled_inference_resizes_source_hint_and_restores_native_output(tmp_path):
    job = make_job(tmp_path, model_scale=0.5, hint_source=1,
                   input_linear=True, output_linear=True)
    calls = []

    def predict(source_dir, hint_dir, destination, cfg):
        import cv2
        assert cfg["model_scale"] == 1.0  # upstream optimized EXR path requires native scale
        for i, path in enumerate(sorted(source_dir.glob("*.exr"))):
            rgb = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
            hint = cv2.imread(str(hint_dir / path.name), cv2.IMREAD_UNCHANGED)
            assert rgb.shape == (2, 2, 3)
            assert hint.shape == (2, 2)
            rgba = np.concatenate([rgb[..., ::-1], np.full((2, 2, 1), 0.5)], axis=-1)
            rgba.astype("<f4").tofile(destination / f"{i:06d}.rgba")
        calls.append(True)

    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predict)
    result = service.process(job)
    output = np.fromfile(result["output"], "<f4").reshape(3, 4, 4)
    np.testing.assert_allclose(output[..., 1], 0.25, atol=1e-6)
    np.testing.assert_allclose(output[..., 3], 0.5, atol=1e-6)
    assert calls == [True]


def test_prepare_publishes_every_display_frame_and_verified_manifest(tmp_path):
    import json

    from worker.cache import file_digest

    job = make_job(tmp_path, input_linear=True, output_linear=True)
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=Predict())
    events = []
    result = service.process(job, progress=lambda fraction, message: events.append((fraction, message)))
    view = Path(result["view_directory"])
    marker = json.loads((view / "complete.json").read_text())
    assert marker["shape"] == {"width": 4, "height": 3, "frames": 2}
    for i in range(2):
        frame = view / f"{i:06d}.rgba"
        assert Path(result["manifest"]) == view / "complete.json"
        assert marker["digests"][i] == file_digest(frame)
        np.testing.assert_allclose(np.fromfile(frame, np.float32)[:4], [0, .25, 0, .5])
    assert events[0][0] == 0 and events[-1][0] == 1
    assert [e[0] for e in events] == sorted(e[0] for e in events)


def test_view_reuse_and_repair_never_rerun_neural_model(tmp_path):
    job = make_job(tmp_path)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    first = service.process(job)
    original = Path(first["view_directory"])
    job["target"] = 0
    assert service.process(job)["view_directory"] == str(original)
    job["settings"]["output_mode"] = 2
    second = service.process(job)
    assert second["view_directory"] != str(original)
    frame = Path(second["view_directory"]) / "000000.rgba"
    np.zeros((3, 4, 4), np.float32).tofile(frame)
    repaired = service.process(job)
    np.testing.assert_allclose(np.fromfile(repaired["output"], np.float32)[:4], [.5, .5, .5, 1])
    assert len(predictor.calls) == 1


def test_failed_view_never_publishes_complete_or_reports_success(tmp_path, monkeypatch):
    job = make_job(tmp_path)
    predictor = Predict()
    service = engine.Engine(tmp_path / "model", tmp_path / "cache", predictor=predictor)
    service.process(job)
    job["settings"]["output_mode"] = 2
    events = []

    def fail(*args):
        raise OSError("disk full")

    monkeypatch.setattr(engine, "compose", fail)
    with pytest.raises(OSError, match="disk full"):
        service.process(job, progress=lambda fraction, message: events.append(fraction))
    assert 1 not in events
    assert len(list(service.cache_root.rglob("views/*/complete.json"))) == 1
    assert not list(service.cache_root.glob("work-*"))
