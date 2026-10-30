"""Re-render the dedicated validation timeline, failing on any host or pixel error."""

import os
import sys
from pathlib import Path

os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
os.environ["RESOLVE_SCRIPT_LIB"] = "/opt/resolve/libs/Fusion/fusionscript.so"
sys.path.insert(0, "/opt/resolve/Developer/Scripting/Modules")

import cv2
import DaVinciResolveScript as dvr
import numpy as np


def main():
    root = Path(__file__).resolve().parents[1]
    resolve = dvr.scriptapp("Resolve")
    manager = resolve.GetProjectManager()
    project = manager.GetCurrentProject()
    if not project or project.GetName() != "CorridorKey Validation":
        project = manager.LoadProject("CorridorKey Validation")
    if not project or project.GetName() != "CorridorKey Validation":
        raise RuntimeError("Cannot open dedicated validation project")
    timeline = project.GetCurrentTimeline()
    item = timeline.GetItemListInTrack("video", 1)[0]
    comp = item.GetFusionCompByIndex(1)
    keyer = comp.FindTool("CorridorKey201")
    saver = comp.FindTool("Saver1")
    saver.ConnectInput("Input", keyer)
    for mode in (0, 1):
        keyer.SetInput("hint_frames", mode)
        prefix = root / f"build/evidence/resolve-hint-{mode}-"
        saver.SetInput("Clip", str(prefix) + ".exr")
        result = comp.Render({"Start": 0, "End": 1, "Wait": True})
        keyer.SetInput("refresh_status", 1)
        status = keyer.GetInput("status", 0)
        print("Render", result, "Status", status, flush=True)
        if not result:
            raise RuntimeError(f"Resolve render failed: {status}")
        frames = sorted(prefix.parent.glob(prefix.name + "*.exr"))
        if len(frames) != 2:
            raise RuntimeError(f"Expected two EXR frames, got {frames}")
        for path in frames:
            frame = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
            assert frame.shape == (256, 384, 4), frame.shape
            assert np.isfinite(frame).all()
            assert frame[..., 3].max() - frame[..., 3].min() > 0.5
        print("Verified two finite RGBA frames with varying alpha:", mode, flush=True)
    manager.SaveProject()
    comp.Save(str(root / "build/evidence/CorridorKey-validation.comp"))


if __name__ == "__main__":
    main()
