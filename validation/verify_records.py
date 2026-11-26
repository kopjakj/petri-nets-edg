#!/usr/bin/env python3
"""Audit saved evidence only; does not execute firmware or start simulations.

Usage: python3 -I verify_records.py [--output validation_summary.json]
The reference player below is independent of runner/run.js. The UART archive
is read in place, never extracted. All reported firings are replayed.
"""
import argparse
import collections
import hashlib
import itertools
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parent
EVIDENCE = ROOT / "startup_audit/remote_results"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def reachable(net):
    """Exact reference graph for the supplied bounded nets, not arbitrary nets."""
    initial = tuple(net["m0"])
    work = [initial]
    seen = {initial}
    for marking in work:
        for transition in net["T"]:
            if not all(marking[p] for p in transition["pre"]):
                continue
            new = list(marking)
            for p in transition["pre"]:
                new[p] -= 1
            for p in transition["post"]:
                new[p] += 1
            new = tuple(new)
            require(max(new) < 256, "Reference counter range exceeded")
            if new not in seen:
                seen.add(new)
                work.append(new)
                require(len(work) <= 100000, "Reference graph limit exceeded")
    return seen


def check_trace(raw, row, net):
    marking = list(net["m0"])
    visited = {tuple(marking)}
    counts = [0] * len(net["T"])
    declared = {}
    declared_m0 = None
    header = None
    ready = started = ended = False
    fires = 0
    for line in raw.decode("utf-8").splitlines():
        timestamp, sep, text = line.partition("\t")
        require(sep and float(timestamp) >= 0, "Malformed timestamp")
        if text.startswith("NET "):
            header = text
        elif text.startswith("T "):
            m = re.fullmatch(r"T (\d+) (\S+) pre((?: \d+)*) post((?: \d+)*)", text)
            require(m, "Malformed transition declaration")
            declared[int(m[1])] = {
                "name": m[2], "pre": [int(v) for v in m[3].split()],
                "post": [int(v) for v in m[4].split()],
            }
        elif text.startswith("M0 "):
            declared_m0 = [int(v) for v in text.split()[1:]]
        elif text == "START":
            started = True
        elif text == "INIT_READY":
            ready = True
        elif text.startswith("F "):
            require(not ended and started, "Firing outside the recorded execution")
            t = int(text.split()[1])
            require(0 <= t < len(counts), "Unknown transition")
            transition = net["T"][t]
            require(all(marking[p] > 0 for p in transition["pre"]),
                    f"Illegal grant: {row['name']} transition {t}")
            for p in transition["pre"]:
                marking[p] -= 1
            for p in transition["post"]:
                marking[p] += 1
            counts[t] += 1
            fires += 1
            visited.add(tuple(marking))
        elif text.startswith("END "):
            m = re.fullmatch(r"END fired=(\d+) qmax=(\d+)", text)
            require(m and int(m[1]) == fires == 2000, "Bad target marker")
            require(int(m[2]) == row["qmax"], "Queue statistic mismatch")
            ended = True
        else:
            require(text == "INIT_BEGIN" or re.fullmatch(r"(?:GROUPS \d+|HEAP free=\d+)", text),
                    f"Unexpected UART line: {text}")
    expected_header = (f"NET {net['name']} np={len(net['m0'])} nt={len(net['T'])} "
                       f"POL {row['polName']} SEED {row['seed']} JIT 1 Q {row['qdepth']}")
    require(header == expected_header, "Firmware configuration does not match row")
    require([declared.get(t) for t in range(len(counts))] == net["T"],
            "Declared topology differs from independent net data")
    require(declared_m0 == net["m0"], "Initial marking differs")
    require(ready and started, "Initialization did not finish")
    enabled = any(all(marking[p] > 0 for p in t["pre"]) for t in net["T"])
    require((row["end"] == "target" and ended) or
            (row["end"] == "dead" and not ended and not enabled), "Bad end classification")
    require(fires == row["fires"] and counts == row["counts"] and marking == row["final"],
            "Replay disagrees with saved firing counts or final reference marking")
    require(visited == {tuple(map(int, m.split(","))) for m in row["markings"]},
            "Visited markings differ from saved record")
    return visited


def searches():
    unique = {}
    files = {}
    for name in ("results_repo.txt", "results_s1.txt"):
        records = []
        for line in (ROOT / "grouped_search" / name).read_text().splitlines():
            if "states=" not in line:
                continue
            item = {"name": line.split()[0]}
            for key in ("states", "illegal", "errors", "lost", "balance", "overflow",
                        "stalls", "badfinal"):
                m = re.search(rf"\b{key}=(\d+)", line)
                require(m, "Missing search field: " + key)
                item[key] = int(m[1])
            item["status"] = ("state cap" if "state cap" in line else
                              "overflow pruned" if "overflow pruned" in line else "complete")
            require(all(item[k] == 0 for k in ("illegal", "errors", "lost", "balance",
                                              "stalls", "badfinal")), "Search found a fault")
            if item["name"] in unique:
                require(item == unique[item["name"]], "Shared search records differ")
            unique[item["name"]] = item
            records.append(item)
        require(records, "No search records")
        files[name] = len(records)
    mutants = (ROOT / "grouped_search/mutants.txt").read_text().splitlines()
    require(sum(line.startswith("DETECTED ") for line in mutants) == 6, "Mutation record incomplete")
    return {"unique_nets": len(unique), "files": files,
            "statuses": dict(collections.Counter(r["status"] for r in unique.values())),
            "states_in_unique_records": sum(r["states"] for r in unique.values()),
            "mutants_reported_detected": 6, "records": list(unique.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    manifest = json.loads((EVIDENCE / "corrected_manifest.json").read_text())
    for name, expected in manifest.items():
        actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        require(actual == expected, "Recorded source/binary hash mismatch: " + name)
    require((ROOT / "fw/lib/edg_pn_choice.c").read_bytes() ==
            (ROOT / "upstream/petri/edg_pn_choice.c").read_bytes(), "Protocol differs from upstream snapshot")
    rows = [json.loads(line) for line in (EVIDENCE / "corrected.jsonl").read_text().splitlines()]
    nets = json.loads((ROOT / "runner/nets.json").read_text())
    require(len(nets) == 19 and len(rows) == 1140, "Wrong experiment dimensions")
    expected = set(itertools.product(range(19), (4, 5), range(1, 11), (2, 4, 16)))
    actual = [(r["net"], r["pol"], r["seed"], r["qdepth"]) for r in rows]
    require(len(set(actual)) == len(actual) and set(actual) == expected,
            "Duplicate or missing experiment configuration")
    graph = [reachable(net) for net in nets]
    visited = collections.defaultdict(set)
    counters = collections.Counter()
    by_depth = {}
    with tarfile.open(ROOT / "startup_audit/remote_traces.tar.gz", "r:gz") as archive:
        members = {m.name: m for m in archive.getmembers() if m.isfile() and m.name.endswith(".trace")}
        require(len(members) == 1140, "Raw trace archive incomplete")
        for row in rows:
            require(not row.get("error") and row["jit"] == 1 and row["target"] == 2000,
                    "Wrong run settings or execution error")
            require(row["schedulerStarted"] and row["initReady"], "Startup incomplete")
            require(all(row[k] == 0 for k in ("illegal", "anomalies", "malformed", "qfull"))
                    and not row["mismatch"], "Error in saved run")
            path = (f"startup_audit/traces/after_n{row['net']}_p{row['pol']}"
                    f"_s{row['seed']}_q{row['qdepth']}.trace")
            states = check_trace(archive.extractfile(members[path]).read(), row, nets[row["net"]])
            require(states <= graph[row["net"]], "Trace leaves the reference graph")
            visited[row["net"], row["pol"]].update(states)
            counters[row["end"]] += 1
            d = by_depth.setdefault(str(row["qdepth"]), {"runs": 0, "target": 0, "dead": 0, "firings": 0})
            d["runs"] += 1
            d[row["end"]] += 1
            d["firings"] += row["fires"]
    total = sum(row["fires"] for row in rows)
    saved = json.loads((EVIDENCE / "corrected_summary.json").read_text())
    require(saved["runs"] == len(rows) and saved["firings"] == total and
            saved["ends"] == dict(counters), "Summary does not match evidence")
    require(total == 1920420 and dict(counters) == {"target": 960, "dead": 180}, "Totals changed")
    coverage = [{"name": net["name"], "reachable": len(graph[n]),
                 "grouped_rr": len(visited[n, 4]), "grouped_rand": len(visited[n, 5]),
                 "max_observed_gap": max(row["gapMax"] for row in rows if row["net"] == n)}
                for n, net in enumerate(nets)]
    result = {"verification": "PASS: recorded evidence replayed; no new simulations",
              "runs": len(rows), "firings": total, "ends": dict(counters),
              "illegal": 0, "anomalies": 0, "malformed": 0, "qfull": 0,
              "topology_mismatches": 0, "traces_replayed": 1140,
              "by_depth": by_depth, "coverage": coverage, "search": searches()}
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
