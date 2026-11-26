"""Remote-only A/B emulator regression, with original protocol unchanged.

Run inside ~/edg_pn_study:
  python3 -I startup_audit/run_ab.py

Refuses to run without the explicit EDG_REMOTE_AUDIT=1 environment flag.
All outputs remain under startup_audit/.
"""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
HERE = ROOT / "startup_audit"


def one(job):
    variant, net, policy, seed, depth = job
    stem = f"{variant}_n{net}_p{policy}_s{seed}_q{depth}"
    env = dict(os.environ, STALL_S="5", MAX_S="30", WALL_MS="300000")
    env["TRACE_OUT"] = str(HERE / "traces" / (stem + ".trace"))
    cmd = ["node", str(ROOT / "runner/run.js"), str(HERE / f"{variant}.bin"),
           str(ROOT / "runner/bootrom.cjs"), str(net), str(policy), str(seed),
           "1", "2000", str(depth)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              env=env, timeout=330)
        if proc.returncode:
            raise RuntimeError(f"exit={proc.returncode}: {proc.stderr}")
        row = json.loads(proc.stdout.strip())
        row["variant"] = variant
        return row
    except Exception as exc:
        return {"variant": variant, "net": net, "pol": policy, "seed": seed,
                "qdepth": depth, "error": str(exc)}


def main():
    if os.environ.get("EDG_REMOTE_AUDIT") != "1":
        raise SystemExit("Set EDG_REMOTE_AUDIT=1 on the remote machine only.")
    (HERE / "traces").mkdir(exist_ok=True)
    nets = json.loads((ROOT / "runner/nets.json").read_text())
    manifest = {}
    for p in [HERE / "before.bin", HERE / "after.bin",
              ROOT / "fw/lib/edg_pn_choice.c", ROOT / "fw/main.c",
              ROOT / "runner/run.js"]:
        manifest[str(p.relative_to(ROOT))] = hashlib.sha256(p.read_bytes()).hexdigest()
    (HERE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    jobs = []
    # Reproduce all 80 originally reported failures, with phase diagnostics.
    for net in (1, 2, 3, 14):
        for policy in (4, 5):
            for seed in range(1, 11):
                jobs.append(("before", net, policy, seed, 2))
    # Repeat all 19 nets at all three capacities with corrected initialization.
    for depth in (2, 4, 16):
        for net in range(len(nets)):
            for policy in (4, 5):
                for seed in range(1, 11):
                    jobs.append(("after", net, policy, seed, depth))
    with (HERE / "ab.jsonl").open("w") as output:
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as pool:
            for count, row in enumerate(pool.map(one, jobs), 1):
                output.write(json.dumps(row) + "\n")
                output.flush()
                if count % 100 == 0 or count == len(jobs):
                    print(f"{count}/{len(jobs)}", flush=True)
    (HERE / "DONE").write_text("completed\n")


if __name__ == "__main__":
    main()
