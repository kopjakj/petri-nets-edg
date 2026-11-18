#!/usr/bin/env python3
"""Check that the explorer detects two deliberate faults in a temporary copy."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


test_dir = Path(__file__).resolve().parent
petri_dir = test_dir.parent
common = petri_dir.parent / "samples" / "common"
original = (petri_dir / "edg_pn.c").read_text()

# Leave a transition at zero after firing, or issue offers even while one
# is open. Each mutation changes the real step function used by the explorer.
mutations = (
    ("no reset", "i < n_inputs; i++", "i < 0; i++", "fork-join", "anomalies"),
    ("re-offer", "s->offered < 0 && s->st.d > 0", "s->st.d > 0", "buffer-2", "unsound"),
)

with tempfile.TemporaryDirectory() as tmp:
    mutated = Path(tmp) / "edg_pn.c"
    explorer = Path(tmp) / "pn_explore"
    for name, old, new, net, metric in mutations:
        if original.count(old) != 1:
            raise SystemExit(f"Mutation site changed: {name}")
        mutated.write_text(original.replace(old, new))
        subprocess.run(
            [
                *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O2",
                f"-I{petri_dir}", f"-I{common}", f"-I{common / 'test/inc'}",
                str(test_dir / "pn_explore.c"), str(mutated), str(common / "edg.c"),
                "-o", str(explorer),
            ],
            check=True,
        )
        output = subprocess.check_output(
            [str(explorer), "-v", "--max=100000", net, "credit"], text=True
        )
        print(f"## {name}")
        print(output, end="")
        if not re.search(rf"\b{metric}=[1-9][0-9]*\b", output):
            raise SystemExit(f"Explorer missed mutation: {name}")
