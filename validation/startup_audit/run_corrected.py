"""Remote-only corrected-startup grouped-offer sweep (1,140 runs).

EDG_REMOTE_AUDIT=1 python3 -I startup_audit/run_corrected.py
No original-startup runs are executed.
"""
import collections
import concurrent.futures
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def main():
    if os.environ.get("EDG_REMOTE_AUDIT") != "1":
        raise SystemExit("Run with EDG_REMOTE_AUDIT=1 on the remote machine only.")
    spec = importlib.util.spec_from_file_location("audit_helper", HERE / "run_ab.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    nets = json.loads((ROOT / "runner/nets.json").read_text())
    assert len(nets) == 19
    expected = "ef970ed6e12e16ec704e335f0c8593024d2f92c54fa3255f83964c54c75707db"
    protocol = hashlib.sha256((ROOT / "fw/lib/edg_pn_choice.c").read_bytes()).hexdigest()
    assert protocol == expected, "The grouped protocol source has changed."
    paths = [HERE / "after.bin", ROOT / "fw/main.c", ROOT / "runner/run.js",
             ROOT / "fw/lib/edg_pn_choice.c", ROOT / "runner/nets.json"]
    manifest = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in paths}
    (HERE / "traces").mkdir(exist_ok=True)
    output = HERE / "corrected.jsonl"
    if output.exists():
        raise SystemExit("corrected.jsonl already exists; refusing to overwrite results.")
    (HERE / "corrected_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    jobs = [("after", n, p, s, d)
            for d in (2, 4, 16)
            for n in range(19)
            for p in (4, 5)
            for s in range(1, 11)]
    rows = []
    t0 = time.monotonic()
    with output.open("x") as stream:
        with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get("EDG_WORKERS", "20"))) as pool:
            futures = {pool.submit(helper.one, job): job for job in jobs}
            for f in concurrent.futures.as_completed(futures):
                row = f.result()
                rows.append(row)
                stream.write(json.dumps(row) + "\n")
                stream.flush()
                if len(rows) % 100 == 0 or len(rows) == len(jobs):
                    print(f"{len(rows)}/{len(jobs)} elapsed={time.monotonic()-t0:.1f}s",
                          flush=True)
    totals = {
        "runs": len(rows),
        "firings": sum(r.get("fires", 0) for r in rows),
        "ends": dict(collections.Counter(r.get("end", "error") for r in rows)),
        "illegal": sum(r.get("illegal", 0) for r in rows),
        "anomalies": sum(r.get("anomalies", 0) for r in rows),
        "malformed": sum(r.get("malformed", 0) for r in rows),
        "mismatches": sum(bool(r.get("mismatch")) for r in rows),
        "qfull": sum(r.get("qfull", 0) for r in rows),
        "invalid_runs": [r for r in rows if r.get("end") not in ("target", "dead")
                         or not r.get("schedulerStarted") or not r.get("initReady")
                         or (r.get("end") == "target" and r.get("fires") != 2000)],
        "errors": [r for r in rows if "error" in r],
        "wall_seconds": round(time.monotonic() - t0, 1),
    }
    (HERE / "corrected_summary.json").write_text(json.dumps(totals, indent=2) + "\n")
    (HERE / "CORRECTED_DONE").write_text("completed\n")
    print(json.dumps(totals), flush=True)
    return int(bool(totals["errors"] or totals["illegal"] or totals["anomalies"]
                     or totals["malformed"] or totals["mismatches"]
                     or totals["qfull"] or totals["invalid_runs"]))


if __name__ == "__main__":
    sys.exit(main())
