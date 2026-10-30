# CorridorKey usage and troubleshooting

A native Linux OpenFX effect backed by the real CorridorKey 2.0 CUDA model.
Built for the Resolve Studio 21.0.1 installation on this machine. The Inspector
uses Resolve's native controls, with an optional **Alpha Hint** image input in Fusion.
This is an independent local integration of CorridorKey, not Corridor Digital's
official paid plugin.

## First test

1. Start Resolve normally from its application launcher, or run
   `bash scripts/launch-resolve.sh` from this repository. Restart an existing
   Resolve session after installing or rebuilding the plugin.
2. In Fusion, add **CorridorKey 2.0** from **OpenFX → Keying** (or search for
   CorridorKey in Select Tool). Connect your greenscreen plate to **Source**.
3. Connecting the node starts a **whole-shot cache pass automatically**. Fusion
   shows green node activity and progress while the model works. Once complete,
   playback reads prepared frames. For large plates, set **Internal Resolution =
   Half**, then click **Apply / Refresh**.
4. Connect the output to a Merge's foreground input. The default output is
   premultiplied RGBA. Choose **Alpha Matte** and **Apply / Refresh** to inspect the key.
5. For a roto or external matte, connect it to **Alpha Hint**, select
   **Hint Source = Alpha Hint Input**, then choose the channel containing the
   matte. Use **Red** for a grayscale image whose alpha is solid white; use
   **Alpha** for a mask carried in the alpha channel. Click **Apply / Refresh**.

## Working with shots

**Cache Whole Shot** is enabled by default. The first requested frame prepares the
complete shot chronologically and writes every output frame to disk. The Inspector
stages subsequent edits: playback continues using the last applied settings until
you click **Apply / Refresh**. Output and despill changes reuse neural predictions;
inference settings generate a new neural result. **Apply Whole Shot** also enables
whole-shot mode. Disable that mode only for deliberate single-frame work.
For a trimmed clip or Fusion generator, disable **Use Clip Frame Range** and set
the inclusive **First Frame** and **Last Frame** in Fusion frame coordinates.
The current frame must be within that range. Apply commits range changes too.

**Hint Frames = First Frame** supplies a matte only at the beginning of the shot.
Enable temporal processing and **Carry Predicted Hints** for propagation. **All
Frames** supplies a hint on every frame. Hint strength conditions the model; it
does not multiply the final alpha. For authoritative per-frame hints, disable
carried hints. A stride no larger than the temporal window is required.

Keep **Automatic GPU Memory** enabled initially. **GPU Budget** is a soft profile
selection budget, not a hard allocation cap. Resolve also needs GPU memory. When
automatic selection is disabled, the temporal window, low-memory mode and the
batch/chunk controls under **Advanced Memory** apply directly.

**Cancel Processing** and host cancellation terminate the owned worker; it starts
again on demand. **Release GPU Memory** unloads the model. **Refresh Status** reads
the last operation/result. Status is deliberately updated on request because
changing a parameter during an OFX render can cause Resolve to request another render.

## Color and output

Use display-coded sRGB/Rec.709-primary RGB for the default input, or enable
**Input Is Linear** for scene-linear RGB. Convert log, ACES or other wide-gamut
material upstream and convert back downstream as needed. These controls change
the transfer function, not color primaries; they do not infer project color management.
Enable **Unpremultiply Input** only when source RGB is multiplied by alpha.
Leave it disabled when source alpha merely carries a hint.

The output choices are premultiplied RGBA, straight RGBA, alpha matte, raw
foreground, checkerboard composite and original. **Linearize Output** returns
unclamped linear RGB. Alpha is always coverage. Output mode, despill and
**Keep Original in Solid Areas** reuse cached neural predictions. Green/blue/auto
despill has average and maximum algorithms and does not change the matte.
Brightness/contrast/pivot precondition model input and are inverted after inference.

Use Resolve's render settings/Deliver page for frame ranges, EXR precision, video
codecs, containers and exported output variants. The upstream CLI's random/start
frame selection is represented by the host's explicit shot range; inference
always consumes those frames in order. Source audio remains a host/timeline concern.

## Install and rebuild

The first pass is installed for the current user. No system plugin directory is
modified. The normal Resolve desktop launcher now includes the local OFX search
path while retaining its existing GPU launch options.

```bash
# Reproduce setup, including the separately downloaded model/runtime:
bash scripts/setup.sh

# After editing local source:
bash scripts/verify.sh
bash scripts/install.sh

# Check dependency advisories, including the CUDA wheel release versions:
uv run python scripts/audit.py
```

Requires Linux x86-64, the Resolve OpenFX SDK in `Developer/OpenFX` relative to
Resolve's installation directory, CMake/Ninja, OpenSSL development headers/libcrypto,
a C++17 compiler with static libstdc++, uv, shellcheck, and an NVIDIA
driver supporting the locked PyTorch 2.13 / CUDA 12.6 build. Python 3.11 and dependency
versions are managed by `uv.lock`. Setup downloads several GB and needs additional
space for the Python environment and working frames.

The bundle is at `OFX/Plugins/CorridorKey2.ofx.bundle`, relative to your user data
directory (`XDG_DATA_HOME`, or `.local/share` relative to your home directory).
It points to the current version under `corridorkey-resolve/releases/` in that
same data directory.
Its `Contents/Resources/runtime.json` records this repository's Python environment
and upstream checkout. Keep this repository, `.venv`, and `.deps` at the paths used
when building. A different location needs a fresh environment, build and installation.
The bundle alone does not include model weights.

To disable the effect, close Resolve. From your user data directory, remove only
the local plugin symlink:

```bash
unlink OFX/Plugins/CorridorKey2.ofx.bundle
```

The existing launcher was backed up as
`applications/DaVinciResolve.desktop.before-corridorkey` in your user data directory.
Leaving the local OFX search path in the launcher is harmless after removal.

## Cache and troubleshooting

The default cache is `corridorkey-resolve` inside your user cache directory
(`XDG_CACHE_HOME`, or `.cache` relative to your home directory).
**Cache Folder** can point to another local SSD. Input pixels and inference
settings are hashed; changed source frames or hints invalidate the neural result.
Content checksums detect upstream changes and damaged cached predictions/output.
Multi-frame render sequences validate the full input dependency set before
emitting cached output. The default 20 GiB limit evicts older
shots, but retains the current shot even if it exceeds the limit.
Staging files left by interrupted inference are reclaimed by the next job.
After editing upstream tools, use **Apply / Refresh** to update the entire
snapshot, including before export. Resolve's Edit page marks ordinary single-frame
requests noninteractive too, so those requests reuse the applied shot. Fusion's
noninteractive sequences still validate every dependency, even for one frame.

Temporal processing stages the whole selected range on disk. At 3840×2160, one
float RGBA frame is about 127 MiB; source, optional hint, working data and predictions
need additional space. Start with short ranges and reduced internal resolution.
Requests have a two-hour ceiling and a 10,000-frame maximum. Preparing a shot runs
the model once; cached playback avoids the worker and reads one verified output
frame. Playback speed still depends on frame size, disk speed, and the rest of the graph.

If a render fails, use **Refresh Status** and inspect `worker.log` in the cache
folder. Missing weights, unavailable source/hint frames, invalid ranges, CUDA
allocation errors and insufficient disk space are reported as errors. For GPU
pressure, release the model, reduce the internal resolution or temporal window,
and try a short range with manual low-memory mode. Other pages can use the effect
on one source; Fusion is the tested workflow for the separate hint input.
`plugin.jsonl` records progress, preparation, cache hits, timings and errors without
screen recording. Progress uses the official OFX suite; Fusion owns its node display.

See [validation](validation.md) for the tested paths and remaining practical
limits, and [third-party notices](../THIRD_PARTY_NOTICES.md) for attribution and licenses.
Source and model revisions are pinned in `scripts/setup.sh`; no source or weights
are sent to a service for inference.
