# Petri nets on Event-Driven Gates (EDG-PN)

A token player for ordinary place/transition nets, built **on top of the
unmodified gate library** in [`../samples/common/`](../samples/common/README.md).
It is the software behind the manuscript *Running Petri nets on event-driven
software gates* (submitted for publication).

| Petri net | Event-driven gates |
|---|---|
| place `p` | level gate (one task, one queue); `d` counts processed deposits minus withdrawals |
| marking `M(p)` | the reference marking, obtained by playing the firing decisions; `d` can lag while messages travel |
| token deposited | message `(true, chg)` → `d++` |
| token withdrawn | message `(false, chg)` → `d--` |
| initial marking | one `(false, init)` per place, then `M0(p)` deposits |
| offer | `(true, chg)` from a place to one consumer; `(true, init)` if the place is the consumer's release input |
| release / request | `(false, init)` / `(true, init)` between a place and its release input |
| "p is marked" | the ordinary gate output `d > 0` (for LEDs and gates) |
| transition `t` | AND gate; `d` = offers still missing; fires at `d == 0` |
| conflict | **offer discipline** plus **release and request** (version 2) |
| external event | token source: rising change of any EDG signal |

## Files

| File | Purpose |
|---|---|
| `edg_pn.h`, `edg_pn.c` | The layer: the place and transition step functions, `edg_pn_auto_release()` (sets every release input from the wiring) and three FreeRTOS task bodies (place, transition, token source). Every descriptor update goes through `edg_gate_step()`. |
| `edg_pn_choice.h`, `edg_pn_choice.c` | Optional choice gate for transitions with more than one shared input: a group collects the offers of a complete preset and grants one firing (used by [`../samples/03-grouped-offers/`](../samples/03-grouped-offers/README.md)). |
| `test/nets.h` | Eleven validation nets and four embedded application nets (job shop, level crossing, car park, sensor choice). |
| `test/pn_unit.c` | Unit tests of the step functions and of `edg_pn_auto_release()`. |
| `test/pn_explore.c` | Exhaustive exploration of **all task interleavings** against a reference token game for the designs `offer`, `offer*`, `release`, `release*` and the naive mapping (safety, anomalies, stalls, coverage). |
| `test/pn_freertos.c`, `test/posix/` | The real task layer under the FreeRTOS POSIX port, with a run-time monitor and a perturbed schedule. |
| `test/check_mutants.py` | Deliberate faults in `edg_pn.c` must be reported by the explorer. |
| `test/run_explore.sh`, `test/run_freertos.sh` | The exploration and FreeRTOS campaigns. |
| `test/emu/run_kitting.js`, `run_mixer.js` | Samples 01 and 02 on an emulated RP2040 (rp2040js) with scripted switch flips and a trace check. |
| `test/results/` | The recorded outputs of the campaign quoted in the manuscript. |

## Guarantees and limits

* **Legal firings for every ordinary net**: every firing is legal, under every
  task interleaving, assuming correct wiring and exactly-once FIFO delivery.
* **No premature quiescence when each transition has at most one shared input**:
  a quiescent closed execution has a dead reference marking. This covers free
  choice, mutual exclusion and choices steered by condition places; the release
  input is then the transition's only shared input.
* A transition with **two shared inputs** cannot tell which place offered, so it
  keeps every offer and can still stall (dining philosophers, `efc-pair`).
  Resolving that needs sender identity (for example one queue per input arc), a
  restructured net, or the optional choice gate in `edg_pn_choice.c`.
* **Legal grants with grouped offers**: every grant of `edg_pn_choice.c` is
  legal under the same delivery assumptions. General progress, starvation
  freedom and a bound on notification storage are not proven; see
  [`../validation/`](../validation/README.md) for the finite test evidence.
* Not supported: arc weights, capacities, inhibitor/read arcs, guards, source
  transitions (use a token source instead).

## Where it can be used

Building blocks that fit the supported class:
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
make                 # unit tests and a few short searches (seconds)
./run_explore.sh     # the full campaign: validation, embedded, naive, traces, tight, mutants
FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel make pn_freertos
./run_freertos.sh    # every net under the POSIX port, both rules, three seeds
python3 check_mutants.py   # deliberate faults must be detected
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

## Results

`test/results/` holds the recorded outputs: `validation.txt`, `embedded.txt`,
`naive.txt`, `traces.txt`, `tight.txt`, `mutants.txt`, the per-net files under
`results/explore/` and the FreeRTOS runs in `freertos.txt`. The manuscript
reports these; Supplementary File S1 contains the full package, including the
table generator.

Not done: a run on physical hardware, a run in Wokwi, any timing / memory /
energy measurement.
