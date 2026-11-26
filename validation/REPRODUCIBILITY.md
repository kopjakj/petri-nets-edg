# Reproduce the grouped-offer validation

This directory is self-contained apart from explicitly downloaded tools and
dependencies. It can be uploaded as `validation/` in a GitHub repository. Start
here; no files in `/tmp/opencode`, the original workspace or the remote machine
are needed. Source origins and precise limitations are in `PROVENANCE.md`.

## 1. Verify the reported results without running simulations

Requirements: Python 3.10+; GNU `sha256sum`.

```sh
cd validation
sha256sum -c SHA256SUMS.txt
python3 -I test_verify_records.py
python3 -I verify_records.py
```

The second command independently replays **every firing in all 1,140 raw UART
traces**, verifies declared net wiring against `runner/nets.json`, verifies the
historical source/binary manifest, rejects missing/duplicate configurations,
recalculates reference reachability and checks saved search-record consistency.
It does not execute an emulator. Expected results:

| Queue depth | Runs | Target (2,000 firings) | Reference-net dead | Firings |
|---|---:|---:|---:|---:|
| 2 | 380 | 320 | 60 | 640,140 |
| 4 | 380 | 320 | 60 | 640,140 |
| 16 | 380 | 320 | 60 | 640,140 |
| **Total** | **1,140** | **960** | **180** | **1,920,420** |

All recorded runs completed initialization. No illegal firing, protocol/route
anomaly, malformed line, topology mismatch, queue-send timeout or protocol stall
was observed. The 180 dead endings are `job-shop`, `job-shop-rev` and
`philosophers-3-split` (60 each), with **no reference transition enabled**.

Saved output: `validation_summary.json`. The reference marking is updated at
each logged group GRANT, before its delivery, not at withdrawal/deposit receipt.
The emulator stops at the target even if other messages are still in flight;
its target endpoints do not establish final local-count coherence.

Release checks are retained under `checks/`: the firmware builds without compiler
warnings, both search variants build, and six remote runs from this relocated
package reproduce the original kitting, five-philosopher and greedy-competitor
records (both grouped policies, seed 1, depth 2). These smoke runs are not added
again to the 1,140-run totals.

## 2. Run the emulator (on a simulation host)

Requirements: Node 18+ and npm, Python 3.10+. No Arm compiler is required when
using the recorded firmware. The full run used 20 parallel processes on a
28-logical-CPU host and took about six minutes; slower machines will take longer.
Set `EDG_WORKERS=4` (for example) to reduce parallelism.
Use a remote machine if simulation must not run on the local PC.

```sh
cd validation/runner
npm ci --no-audit --no-fund
python3 -I fetch_bootrom.py

# One corrected grouped round-robin run: net 3 = kitting, seed 1, depth 2.
STALL_S=5 MAX_S=30 WALL_MS=300000 \
node run.js ../startup_audit/after.bin bootrom.cjs 3 4 1 1 2000 2
cd ..

# Complete grouped-only matrix; explicitly opt in to running simulations.
EDG_REMOTE_AUDIT=1 python3 -I startup_audit/run_corrected.py
```

The full driver refuses to overwrite `startup_audit/corrected.jsonl`. The
shipped historical data live in `startup_audit/remote_results/` and are not
overwritten. New per-run traces go to `startup_audit/traces/`. For a second full
run, use a fresh copy of this folder, not a destructive reset of the evidence.

Matrix: **19 nets × 2 policies × seeds 1–10 × depths 2/4/16**. The policies are
ID 4 (`grouped-rr`, original rotating scan) and ID 5 (`grouped-rand`, experimental
random scan start). Scheduling is perturbed by 0–3 `taskYIELD()` calls before
each handler. This is deterministic seeded task scheduling, not a stochastic
Petri net or random hardware interrupts. Neither old offer/release policies nor
the central baseline are included in this corrected sweep.

Stops: 2,000 logged firing decisions; five simulated seconds without a firing;
30 simulated seconds overall; 300 wall-clock seconds in Node (330-second Python
subprocess timeout); or a queue-send timeout/diagnostic. A grouped send reports
`QFULL` after three simulated seconds; the driver stops at that diagnostic.
The replay distinguishes a dead marking from a protocol stall by reference
enabling. Zero stalls in these finite runs is not a general progress guarantee.

Models (C sources in `fw/nets.h`, `fw/nets_extra.h`; JSON used for checking in
`runner/nets.json`, in the same index order):

```text
 0 fork-join             1 buffer-2              2 choice-merge
 3 kitting               4 mutex                 5 mutex-idle
 6 philosophers-2        7 philosophers-3         8 efc-pair
 9 sensor-choice       10 sensor-choice-2       11 job-shop
12 job-shop-rev         13 level-crossing        14 car-park
15 philosophers-5      16 philosophers-3-split  17 mutex-3
18 greedy-pair
```

The kitting net here is the **closed four-place validation net**, not the open
seven-place switch-driven application. The ordinary philosophers take both
forks in one transition; `philosophers-3-split` takes forks sequentially and has
a dead marking in the reference net itself.

## 3. Rebuild from firmware source

Requirements: CMake 3.13+, Ninja, Git, Arm GCC 16.2.0 and the Pico SDK/FreeRTOS
snapshots below. Download dependencies into this folder, not system locations:

```sh
cd validation
mkdir -p deps
git clone https://github.com/raspberrypi/pico-sdk.git deps/pico-sdk
git -C deps/pico-sdk checkout --detach 98a542c1a62fb549ffb5d66a3e5892b06276b670
git -C deps/pico-sdk submodule update --init --recursive
git clone https://github.com/raspberrypi/FreeRTOS-Kernel.git deps/FreeRTOS-Kernel
git -C deps/FreeRTOS-Kernel checkout --detach 4f7299d6ea746b27a9dd19e87af568e34bd65b15
export PICO_SDK_PATH="$PWD/deps/pico-sdk"
export FREERTOS_KERNEL_PATH="$PWD/deps/FreeRTOS-Kernel"
bash build_firmware.sh

# On the simulation host, test the rebuilt image rather than the saved image:
cd runner
STALL_S=5 MAX_S=30 WALL_MS=300000 \
node run.js ../build/study.bin bootrom.cjs 3 4 1 1 2000 2
```

The build is RP2040, one core, 10 Hz periodic tick, 200 KiB heap. Different
compiler/SDK versions and paths may change binary bytes and schedules; the
recorded binary and manifest remain the authority for the historical numbers.
Do not overwrite the shipped `startup_audit/after.bin` merely to test a rebuild.
For a full rebuilt-image sweep, use a **separate copy** of the package, replace
`startup_audit/after.bin` there with `build/study.bin`, and run the same matrix;
its newly written manifest records the new image hash.

To regenerate the JSON net definitions independently of the Arm build:

```sh
cc -std=c11 -Ifw runner/gen_nets.c -o build/gen_nets
build/gen_nets > build/nets-regenerated.json
python3 -I -c 'import json; assert json.load(open("build/nets-regenerated.json")) == json.load(open("runner/nets.json"))'
```

## 4. Reproduce the larger grouped searches

Requirements: a C11 compiler, GNU linker/Make/Bash, Python 3. Several million
states require several GiB of RAM. Run these on a suitable computation host.

```sh
cd validation/grouped_search
bash build.sh
GX_MAX=5000000 ./gx_repo > rerun_repo.txt
GX_MAX=5000000 ./gx_s1 > rerun_s1.txt
bash mutants.sh > rerun_mutants.txt
```

`gx_repo` uses the original GitHub place core and eleven-net set; `gx_s1` uses
the same core with S1's fifteen nets. Both include the same eight small
sample/additional nets, hence 26 unique nets over 42 rows. `gx_s1lib` is an
optional compatibility build using the S1 place core, not the source of the
two recorded full-search files.

The explorer branches over all FIFO mailbox heads, with each handler and its
posts atomic. It checks grant legality against an independent marking, request
presence, in-flight token accounting, quiescent enabling and final local counts.
Defaults: **16 events/mailbox, 96 total, 5,000,000 states** for the saved runs.
It **prunes overflowing successors**, not models blocking-send backpressure.
Exit zero means no checked fault was found; it does **not** mean a complete
search. Read the `complete` / `INCOMPLETE` label on every row.

| Unique search instances | Number |
|---|---:|
| Complete | 12 |
| Incomplete: overflow-pruned | 7 |
| Incomplete: state cap | 7 |

All recorded fault counters are zero on explored paths. Six temporary protocol
mutations were reported detected; `grant-first` was distinguished only by a
finite coverage difference in a capped comparison, so it is not evidence of a
general correctness violation. Mutation outputs are detector sanity checks,
not a comprehensive mutation score or fairness proof.

`results_repo.txt`, `results_s1.txt` and `mutants.txt` are the historical records;
write reruns to new filenames. The encoder/reference graph is intended only for
the supplied bounded nets, not an arbitrary unbounded-net verifier.

## 5. Startup regression and limits

`startup_audit/remote_results/baseline_kitting.*` records the original adapter
filling a two-slot queue **before the scheduler starts** (zero firings). The
corrected initialization task sends INITs first and then deposits after startup,
allowing workers to drain queues. `fixed_kitting.*` reaches 2,000 firings. The
failure was in our adapter, not the unchanged grouped protocol. The old claim
that this demonstrated a runtime grouped deadlock is withdrawn.

Optional A/B reproduction (also runs the full corrected matrix):

```sh
EDG_REMOTE_AUDIT=1 python3 -I startup_audit/run_ab.py
```

The original adapter binary and source are retained **only** for this regression,
not recommended for new simulations. Boot ROM acquisition/licensing is described
in `runner/THIRD_PARTY.md`; ROM bytes and npm dependencies are not redistributed.

Passing the tested depths is not a universal queue bound. Qualitative fair
scheduling does not bound the backlog from an arbitrarily delayed subscriber.
Random scan starts do not provide uniform transition choice or starvation
freedom. In the corrected sweep, both policies visited kitting 7/9, car-park
77/109 and mutex-3 17/20 reference markings (union across seeds and depths).
These gaps are sampling results, not proofs of unreachable behaviour. No
physical-board timing, multicore or actuator-coherence claim is made.
