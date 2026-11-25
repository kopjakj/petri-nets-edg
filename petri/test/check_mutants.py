#!/usr/bin/env python3
"""Check that the explorer detects deliberate faults in temporary copies of
lib/edg_pn.c. Each mutation changes the real step functions the explorer
drives; the run must report the stated failure."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
lib = here.parent
common = here.parents[1] / "samples" / "common"
original = (lib / "edg_pn.c").read_text()

#  name, text to replace, replacement, net, design, metric that must be > 0
MUTATIONS = (
    ("no reset: a transition keeps d = 0 after firing",
     "i < n_kept; i++", "i < 0; i++", "fork-join", "offer", "anomalies"),
    ("re-offer: a place offers while an offer is open",
     "if (s->offered < 0 && s->st.d > 0 && n_consumers > 0) {",
     "if (s->st.d > 0 && n_consumers > 0) {", "buffer-2", "offer", "unsound"),
    ("fire early: the release-input offer fires before the kept offers",
     "if (s->st.d == 0) {", "if (1) {", "sensor-choice", "release", "unsound"),
    ("no request: a transition never asks for a released token",
     "return EDG_PN_REQUEST;", "return EDG_PN_WAIT;", "sensor-choice", "release", "stalls"),
)

failed = False
with tempfile.TemporaryDirectory() as tmp:
    mutated = Path(tmp) / "edg_pn.c"
    explorer = Path(tmp) / "pn_explore"
    for name, old, new, net, design, metric in MUTATIONS:
        if original.count(old) != 1:
            raise SystemExit(f"mutation site not unique or missing: {name}")
        mutated.write_text(original.replace(old, new))
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O2", "-w",
                        f"-I{lib}", f"-I{common / 'test' / 'inc'}", f"-I{common}", str(here / "pn_explore.c"),
                        str(mutated), str(common / "edg.c"), "-o", str(explorer)], check=True)
        out = subprocess.check_output([str(explorer), "-v", "--max=300000", net, design], text=True)
        hit = re.search(rf"\b{metric}=([1-9][0-9]*)\b", out)
        first = re.search(r"first (illegal firing|place anomaly|stall)[^\n]*\n\s*trace \((\d+) steps", out)
        print(f"## {name}: {net} {design}: {metric}="
              f"{hit.group(1) if hit else 0}"
              f"{f', first {first.group(1)} after {first.group(2)} steps' if first else ''}"
              f"  -> {'DETECTED' if hit else 'MISSED'}")
        failed |= not hit
raise SystemExit(1 if failed else 0)
