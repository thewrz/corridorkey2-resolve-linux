"""Measured progress without modifying the upstream model implementation."""

import re


class Progress:
    def __init__(self, callback=None):
        self.callback = callback or (lambda fraction, message: None)
        self.fraction = 0.0

    def __call__(self, fraction, message):
        self.fraction = max(self.fraction, min(1.0, float(fraction)))
        self.callback(self.fraction, message)


class InferenceProgress:
    """Tee upstream logs and combine overlapping stage-1/stage-2 work counters."""

    def __init__(self, stream, progress, frames):
        self.stream, self.progress, self.frames = stream, progress, frames
        self.pending = ""
        self.windows = 0.0
        self.done = 0

    def write(self, text):
        self.stream.write(text)
        self.pending += text
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            match = re.search(r"window (\d+)/(\d+) \[", line)
            if match:
                self.windows = min(1, int(match[1]) / max(1, int(match[2])))
                self.report(f"Temporal window {match[1]} / {match[2]}")
            elif "RVM continuous prepass" in line:
                self.report("Recurrent matte prepass")
            elif "frozen VFM" in line:
                self.report("Foundation feature prepass")
        # Do not retain arbitrary third-party output without newlines indefinitely.
        self.pending = self.pending[-8192:]
        return len(text)

    def flush(self):
        self.stream.flush()

    def frames_done(self, count):
        self.done = min(self.frames, self.done + count)
        self.report(f"Reconstructing frame {self.done} / {self.frames}")

    def report(self, message):
        self.progress(round(.20 + .42 * self.windows + .23 * self.done / self.frames, 8), message)
