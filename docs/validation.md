# Validation

These checks were performed on 30 October 2026 with DaVinci Resolve Studio
21.0.1, build 11, on Linux. The tested hardware and software versions are listed
in the [README](../README.md#tested-machine).

## Automated checks

Run the build, native contracts, Python tests and linters with:

```bash
bash scripts/verify.sh
```

The publication build passes 6 CTest suites and 36 Python tests, plus Ruff and
ShellCheck. The private session-specific acceptance script and its log-reader
test were retired during repository cleanup; product regression tests remain.

Native tests cover image layout, worker lifetime and cancellation, applied
settings, output cache integrity, optional host notifications, C++ runtime
isolation, and the actual OpenFX bundle's Filter and General context contracts.
Python tests cover worker input validation, color conversion, output modes,
model compatibility, cache reuse and corruption, progress, and installation.

## Live Resolve checks

| Check | Observed result |
| --- | --- |
| Fusion first connection | Automatically prepared a 306-frame, 640 x 360 shot; initial processing took about 102 seconds |
| Fusion cached playback | 90 cached renders in about 3 seconds; median plugin render 2.98 ms; no new preparations |
| Pending Inspector changes | Output mode, resolution and brightness changes remained pending until Apply / Refresh |
| Apply display changes | Rebuilt the complete display view while reusing neural predictions |
| Save and reload | Applied settings and RGBA output survived a Resolve restart |
| Fusion progress | Host progress calls succeeded; the node displayed activity and a progress strip |
| External hints | Synthetic two-frame renders exercised both all-frame and first-frame hints |
| Fusion EXR output | Two output frames followed full-shot input validation; finite RGBA with varying alpha |
| Edit drag and drop | Effect applied directly from the OpenFX library to a clip and appeared in Inspector > Effects |
| Edit first connection | Automatically prepared all 240 frames at 384 x 256 in about 48 seconds |
| Edit cached playback | 76 cache hits over a three-second playback check on a 24 fps timeline; typical plugin reads about 1.2 ms; no new preparations or render errors |

Measurements apply to these clips and the tested machine. They are not
production-resolution benchmarks. Raw captures, project exports and private
session records are not distributed with this repository.

## Cache behavior

Inspector edits wait for **Apply / Refresh**. Use that button after changing
upstream nodes too. The committed shot remains available while edits are pending.

Resolve's Edit host marks ordinary single-frame requests noninteractive, so
Filter instances reuse the committed shot for those requests. Multi-frame
sequences validate the full dependency set once. Fusion's General instances
also validate noninteractive single-frame sequences. Regression tests cover
these distinctions and changes to later source frames.

Filter instances expose Source and Output. General instances retain the
optional Alpha Hint input for Fusion. The context test loads the built plugin
and checks both descriptors and instance creation.

The Effects library's **Hover Scrub Preview** can show unrelated sample footage.
Disabling that Resolve preference avoids model work while browsing effects;
it does not prevent an applied clip from processing automatically.

## Limits

The free edition and Color-page workflow have not been tested directly. The
separate Alpha Hint input is qualified in Fusion. Production 4K/8K shots,
difficult greenscreen footage, simultaneous instances and Resolve Deliver
configurations need further real-workload evaluation.

Initial processing is expensive. Cached playback depends on the frame size,
disk speed and the rest of the graph. Color-space transforms must be configured
explicitly; the plugin does not infer project color science.
