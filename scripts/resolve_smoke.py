"""Render only the dedicated CorridorKey Validation project created for this test."""

import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, "/opt/resolve/Developer/Scripting/Modules")
os.environ["RESOLVE_SCRIPT_LIB"] = "/opt/resolve/libs/Fusion/fusionscript.so"
import DaVinciResolveScript as dvr


def main():
    resolve = dvr.scriptapp("Resolve")
    if not resolve:
        raise RuntimeError("Launch Resolve with scripts/launch-resolve.sh first")
    manager = resolve.GetProjectManager()
    project = manager.GetCurrentProject()
    if not project or project.GetName() != "CorridorKey Validation":
        raise RuntimeError("Open the dedicated CorridorKey Validation project first")
    project.SetSetting("timelineResolutionWidth", "384")
    project.SetSetting("timelineResolutionHeight", "256")
    timeline = project.GetMediaPool().CreateEmptyTimeline("CorridorKey test")
    if not timeline:
        raise RuntimeError("Test timeline already exists; refusing to replace it")
    project.SetCurrentTimeline(timeline)
    item = timeline.InsertFusionCompositionIntoTimeline()
    comp = item.GetFusionCompByIndex(1)
    evidence = ROOT / "build/evidence"
    evidence.mkdir(parents=True, exist_ok=True)
    for index in range(2):
        source = np.fromfile(ROOT / f"build/smoke/job/source_{index:06d}.rgba", "<f4").reshape(256, 384, 4)
        cv2.imwrite(str(evidence / f"plate{index:06d}.png"),
                    (source[..., [2, 1, 0]] * 255).astype(np.uint8))
        cv2.imwrite(str(evidence / f"hint{index:06d}.png"), (source[..., 3] * 255).astype(np.uint8))
    comp.Lock()
    try:
        plate = comp.AddTool("Loader", -2, 0)
        plate.SetInput("Clip", str(evidence / "plate000000.png"))
        hint = comp.AddTool("Loader", -2, 2)
        hint.SetInput("Clip", str(evidence / "hint000000.png"))
        keyer = comp.AddTool("ofx.org.corridorkey.community.resolve.v2", 0, 0)
        if not keyer:
            raise RuntimeError("CorridorKey OpenFX is not registered")
        keyer.ConnectInput("Source", plate)
        keyer.ConnectInput("AlphaHint", hint)
        keyer.SetInput("hint_source", 2)
        keyer.SetInput("hint_channel", 1)
        keyer.SetInput("auto_vram", 0)
        keyer.SetInput("low_vram", 1)
        keyer.SetInput("auto_range", 0)
        keyer.SetInput("shot_start", 0)
        keyer.SetInput("shot_end", 1)
        keyer.SetInput("hann_chunk", 2)
        keyer.SetInput("hann_stride", 1)
        keyer.SetInput("temporal", 1)
        outputs = comp.GetToolList(False, "MediaOut")
        if outputs:
            next(iter(outputs.values())).ConnectInput("Input", keyer)
        saver = comp.AddTool("Saver", 2, 2)
        saver.ConnectInput("Input", keyer)
        saver.SetInput("Clip", str(evidence / "resolve-keyed.exr"))
        saver.SetInput("OutputFormat", "OpenEXRFormat")
        comp.SetAttrs({"COMPN_GlobalStart": 0, "COMPN_GlobalEnd": 1,
                       "COMPN_RenderStart": 0, "COMPN_RenderEnd": 1})
        comp.CurrentTime = 0
        comp.ActiveTool = keyer
    finally:
        comp.Unlock()
    comp.Save(str(evidence / "CorridorKey-validation.comp"))
    manager.SaveProject()
    (evidence / "fusion-inputs.json").write_text(json.dumps(
        {str(k): v.GetAttrs() for k, v in keyer.GetInputList().items()}, indent=2, default=str))
    print("Resolve", resolve.GetVersionString(), "plugin", keyer.GetAttrs(), flush=True)
    result = comp.Render({"Start": 0, "End": 1, "Wait": True})
    keyer.SetInput("refresh_status", 1)
    status = keyer.GetInput("status", 0)
    print("Render", result, "Status", status, flush=True)
    if not result:
        raise RuntimeError(f"Resolve render failed: {status}")
    comp.Save(str(evidence / "CorridorKey-validation.comp"))


if __name__ == "__main__":
    main()
