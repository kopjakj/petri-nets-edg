"""Verify that the request-accounting check detects a discarded pending request."""
from pathlib import Path
from tempfile import TemporaryDirectory
import os
import shlex
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[1]
lib = root / "petri"
edg = here.parents[0] / "common"
stubs = edg / "test" / "inc"
source = (lib / "edg_pn_choice.c").read_text()
before = "if (!(g->pending & EDG_CHOICE_BIT(t)) || (g->open & inputs) != inputs)\n            continue;"
after = """if (!(g->pending & EDG_CHOICE_BIT(t))) continue;
        if ((g->open & inputs) != inputs) {
            g->pending &= ~EDG_CHOICE_BIT(t);
            continue;
        }"""
if source.count(before) != 1:
    raise SystemExit("expected exactly one mutation site")
with TemporaryDirectory() as tmp:
    path = Path(tmp)
    mutant = path / "choice.c"
    mutant.write_text(source.replace(before, after))
    exe = path / "check"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O2",
                    "-ffunction-sections", "-Wl,--gc-sections", "-I" + str(lib),
                    "-I" + str(stubs), "-I" + str(edg), str(here / "main.c"), str(mutant),
                    str(lib / "edg_pn.c"), str(edg / "edg.c"), "-o", str(exe)], check=True)
    result = subprocess.run([str(exe), "--check"], text=True, capture_output=True)
    if result.returncode == 0 or "lost or duplicate request" not in result.stderr:
        raise SystemExit("lost-request mutant did not trigger the request-accounting check")
    print("Discard incomplete pending request: DETECTED by request accounting.")
