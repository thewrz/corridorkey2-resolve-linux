import io

from worker.progress import InferenceProgress, Progress


def test_interleaved_windows_and_stage2_report_monotonic_real_work():
    events = []
    progress = Progress(lambda fraction, message: events.append((fraction, message)))
    log = io.StringIO()
    inference = InferenceProgress(log, progress, 4)
    inference.write("Hann stage1: chunk=4, stride=2, win")
    inference.write("dows=2\n")
    inference.write("      window 1/2 [0:4]\n")
    inference.frames_done(2)
    inference.write("      window 2/2 [2:4]\n")
    inference.frames_done(2)
    values = [event[0] for event in events]
    assert len(set(values)) >= 4
    assert values == sorted(values)
    assert values[-1] == .85  # prediction complete; output cache is still unpublished
    assert "window 2/2" in log.getvalue()


def test_progress_never_regresses_or_reaches_success_early():
    events = []
    progress = Progress(lambda value, message: events.append(value))
    progress(.5, "Half")
    progress(.2, "Earlier phase emitted late")
    assert events == [.5, .5]
