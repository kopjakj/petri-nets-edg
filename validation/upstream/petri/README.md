# Petri nets on Event-Driven Gates (EDG-PN)

A token player for ordinary place/transition nets, built **on top of the
unmodified gate library** in [`../samples/common/`](../samples/common/README.md).
It is the software behind the manuscript *Running Petri Nets on Event-Driven
Software Gates* (MDPI *Eng*).

| Petri net | Event-driven gates |
|---|---|
| place `p` | level gate (one task, one queue) |
| marking `M(p)` | the distance descriptor `d` of that gate |
| token deposited | message `(true, ·)` → `d++` |
| token withdrawn | message `(false, chg)` → `d--` |
| initial marking | init messages: `M0(p)` × `(true, init)`, or one `(false, init)` |
| "p is marked" | the ordinary gate output `d > 0` (for LEDs and gates) |
| transition `t` | AND gate; `d` = input places not yet offering a token; fires at `d == 0` |
| conflict | **offer discipline**: a place offers one token to one output transition at a time |
| external event | token source: rising change of any EDG signal |

## Files

| File | Purpose |
|---|---|
| `edg_pn.h`, `edg_pn.c` | The layer: two pure step functions (`edg_pn_place_step`, `edg_pn_trans_step`) and three FreeRTOS task bodies (place, transition, token source). Every descriptor update goes through `edg_gate_step()`. |
| `test/nets.h` | The eight validation nets and the three application examples as closed nets (A1 IoT gateway, A2 batch mixer, A3 EV charging site). |
| `test/pn_unit.c` | Unit tests of the two step functions. |
| `test/pn_explore.c` | Exhaustive exploration of **all task interleavings** against a reference token game (safety, anomalies, stalls, coverage); also explores the naive mapping, to show why it fails. |
| `test/pn_freertos.c`, `test/posix/` | The real task layer under the FreeRTOS POSIX port, with a run-time monitor and a perturbed schedule. |
| `test/emu/run_kitting.js`, `run_mixer.js` | Samples 13 and 14 on an emulated RP2040 (rp2040js) with scripted switch flips and a trace check. |
| `test/run_examples.sh` | Validation of the three application examples: FreeRTOS POSIX runs and a bounded exploration. |
| `test/make_tables.py` | Regenerates the tables and totals of the manuscript from `test/results/`. |
| `test/results/` | The result files quoted in the manuscript. |

## Guarantees and limits

* **Safe for every ordinary net**: every firing is legal, under every task interleaving.
* **Complete for conflict-free and free-choice nets**: the network stops only in a dead marking.
* Other nets (a transition that competes for a shared place *and* waits for
  other inputs — mutual exclusion, dining philosophers) stay safe but a token
  can be *parked* at a transition that cannot use it. This is the limit of
  anonymous messages; see the manuscript, Section 6.1.
* Not supported: arc weights, capacities, inhibitor/read arcs, guards, source
  transitions (use a token source instead).

## Where it can be used

Building blocks that fit the supported class (manuscript, Section 6):
sequences of steps, parallel branches with a join, events and conditions (a
token source on the rising change of any gate output), counters and queues,
pools of identical resources, rotating dispatch, timed steps, actuators on the
"marked" level of a place.

| Field | Example | Where |
|---|---|---|
| Sequential control | batch mixer: fork/join, step conditions formed by AND gates, step timer from the DELAY object | [`../samples/02-batch-mixer/`](../samples/02-batch-mixer/README.md), net A2 |
| Manufacturing | kitting cell: rotating dispatch, timed activities, pairwise join | [`../samples/01-kitting-cell/`](../samples/01-kitting-cell/README.md) |
| IoT | data-collection gateway: one reading per node merged into a frame, transmit window of two | net A1 in `test/nets.h` |
| Energy management | EV charging site: vehicles assigned to the three phases in turn, per-phase charging budget | net A3 in `test/nets.h` |

What does **not** fit: an alternative decided by which event comes first or by
the state of another place (finished vs. fault, priorities), and a resource
shared by *named* clients (one power budget for two particular loads). Both are
the mutual-exclusion structure: safe, but they can stall.

## Reproducing the results

```bash
cd test
make                 # unit tests, three small nets, one naive counterexample (seconds)
./run_explore.sh     # the full table (about 15 min, about 4 GB RAM)
FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel make pn_freertos
./run_freertos.sh    # 24 runs of 1,000,000 firings (about 20 min)
./run_examples.sh    # the three application examples (about 20 min, about 4 GB RAM)
python3 make_tables.py   # tables + totals for the manuscript
```

Emulated RP2040 (optional; needs Node.js, `npm install rp2040js`, and the
RP2040 B1 boot ROM from `demo/bootrom.ts` of
<https://github.com/wokwi/rp2040js>, saved as a CommonJS module that exports
`bootromB1`):

```bash
# build sample 01 for the RP2040 with the periodic tick (the emulator does not
# reproduce tickless idle faithfully):
echo 'add_compile_definitions(configUSE_TICKLESS_IDLE=0)' > /tmp/notickless.cmake
cmake -S ../../samples/01-kitting-cell -B /tmp/b01 -G Ninja -DEDG_TARGET=sim \
      -DCMAKE_PROJECT_INCLUDE=/tmp/notickless.cmake && ninja -C /tmp/b01
arm-none-eabi-objcopy -O binary /tmp/b01/edg01_kitting_cell.elf /tmp/fw01.bin
node emu/run_kitting.js /tmp/fw01.bin /path/to/bootrom.js 12
# the same for samples/02-batch-mixer with emu/run_mixer.js (15 s)
```

## Results (2026-11-15, examples 2026-11-16)

| Level | What ran | Outcome |
|---|---|---|
| Step functions | 15 complete explorations, 62,159,540 states, 5,356,005 firing steps | 0 illegal firings, 0 anomalies; no stall on conflict-free / free-choice nets; stalls found on `mutex-idle` and (arbitrary choice) `philosophers` |
| Naive mapping | 8 bounded explorations | illegal firings in all 8, shortest after 4–48 steps |
| Task layer | 24 runs under the FreeRTOS POSIX port (8 nets × 3 seeds); 21 reached 1,000,000 firings, 21,008,753 in total | 0 illegal, 0 anomalies; `mutex-idle` stalls as predicted (`philosophers-2` added 2026-11-18) |
| Emulated RP2040 | sample 01, 11 firings; sample 02, 12 firings over two batches | all legal; LEDs as expected |
| Application examples (closed nets A1–A3) | 9 runs under the FreeRTOS POSIX port, 9,001,432 firings; bounded exploration, 30 million states each | 0 illegal, 0 anomalies; the explorations are incomplete (markings reached: 31/144, 14/14, 59/225) |

Not done: a run on physical hardware, a run in Wokwi, any timing / memory /
energy measurement.
