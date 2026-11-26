# Petri nets on Event-Driven Gates (EDG-PN)

Source code of the manuscript **“Running Petri nets on event-driven software
gates”** (submitted for publication).

Ordinary place/transition nets are executed by the unmodified Event-Driven Gates
(EDG) library: a **place** is a level gate whose descriptor `d` counts the
deposits and withdrawals it has processed, a **token** is a change message, and
a **transition** is an AND gate that fires when `d == 0`. Two rules keep the
token game legal without any sender identity: a place *offers one token at a
time to one consumer*, and a transition that cannot use the offer of its one
shared input *releases it and requests it again* when its other inputs are
complete. For transitions with several shared inputs, an optional indexed
choice group grants a complete preset in one step.

| Petri net | Event-driven gates |
|---|---|
| place `p` | level gate (one task, one queue) |
| processed token count of `p` | the distance descriptor `d` of that gate; it equals the marking `M(p)` only when no token message for `p` is in flight |
| token deposited | message `(true, chg)` → `d++` |
| token withdrawn | message `(false, chg)` → `d--` |
| initialization | one `(false, init)` per place, then `M0(p)` deposits |
| offer | `(true, chg)` from a place to one consumer; `(true, init)` if the place is the consumer's release input |
| release / request | `(false, init)` / `(true, init)` between a place and its release input |
| transition `t` | AND gate; `d` = offers still missing |
| “place is marked” | the ordinary gate output `d > 0` |

## Layout

| Path | Contents |
|---|---|
| [`petri/`](petri/README.md) | The Petri-net layer (`edg_pn.c`, `edg_pn.h`), the grouped-offer module (`edg_pn_choice.c`, `edg_pn_choice.h`), their tests and the recorded results quoted in the manuscript |
| [`samples/common/`](samples/common/README.md) | The EDG gate library (`edg.c`, `edg.h`), the FreeRTOS configuration and the host-test harness |
| [`samples/01-kitting-cell/`](samples/01-kitting-cell/README.md) | Sample 01: kitting cell — rotating dispatch, timed activities, pairwise join |
| [`samples/02-batch-mixer/`](samples/02-batch-mixer/README.md) | Sample 02: batch mixer — sequential control with fork/join |
| [`samples/03-grouped-offers/`](samples/03-grouped-offers/README.md) | Host example of the optional choice gate for transitions with multiple shared inputs |
| [`samples/BUILDING.md`](samples/BUILDING.md) | Toolchain, Pico SDK, Wokwi and hardware build steps |
| [`validation/`](validation/README.md) | Grouped-offer validation: FreeRTOS adapter, 1,140-run RP2040-emulator campaign with all UART traces, handler-boundary searches and an independent record checker |

## Quick start — host tests (no hardware)

```bash
cd petri/test
make                    # unit tests + a few short interleaving searches (seconds)
./run_explore.sh        # the full exploration campaign (~10 min, under 1 GB RAM)
python3 check_mutants.py  # deliberate faults in the layer must be detected
```

`test/results/` holds the recorded outputs of that campaign; Supplementary File
S1 of the manuscript contains the full package, including the table generator.

Run the grouped-offer example with `make -C samples/03-grouped-offers run check`.

## Grouped-offer validation

[`validation/`](validation/README.md) holds the evidence for the grouped-offer
module: a FreeRTOS adapter around a byte-identical copy of `edg_pn_choice.c`,
the 1,140-run RP2040-emulator campaign (19 nets, two selection policies, ten
seeds, three queue depths) with every raw UART trace, and the handler-boundary
searches. The archived records can be checked without an emulator:

```bash
cd validation
sha256sum -c SHA256SUMS.txt
python3 -I test_verify_records.py
python3 -I verify_records.py
```

Full instructions are in [`validation/REPRODUCIBILITY.md`](validation/REPRODUCIBILITY.md);
[`validation/PROVENANCE.md`](validation/PROVENANCE.md) separates the protocol
source from the adapter code.

## The two Pico samples

The SDK and the FreeRTOS kernel are git submodules under `pico/`:

```bash
git clone --recurse-submodules https://github.com/kopjakj/petri-nets-edg.git
cd petri-nets-edg
```

If they live elsewhere, pass `-DPICO_SDK_PATH=... -DFREERTOS_KERNEL_PATH=...`
or set the environment variables of the same names.

```bash
# RP2040 target (Raspberry Pi Pico, what Wokwi emulates)
cmake -S samples/01-kitting-cell -B build/01-sim -G Ninja -DEDG_TARGET=sim
ninja -C build/01-sim

# RP2350 target (Raspberry Pi Pico 2 W)
cmake -S samples/01-kitting-cell -B build/01-hw -G Ninja -DEDG_TARGET=hw
ninja -C build/01-hw
```

The same `-S samples/02-batch-mixer` builds the mixer. Full details,
including the Wokwi setup and flashing, are in [`samples/BUILDING.md`](samples/BUILDING.md).

## FreeRTOS POSIX runs and the RP2040 emulator

```bash
cd petri/test
FREERTOS_KERNEL_PATH=../../pico/FreeRTOS-Kernel make pn_freertos
./run_freertos.sh       # every net, both rules, three seeds
```

The emulator scripts (`test/emu/run_kitting.js`, `test/emu/run_mixer.js`) need
Node.js and `rp2040js`; see [`petri/README.md`](petri/README.md).

## What the code guarantees

* **Legal firings for every ordinary net** — every firing is legal under every
  task interleaving (correct wiring, exactly-once FIFO delivery).
* **No premature quiescence when each transition has at most one shared input** —
  a quiescent closed execution then has a dead marking. This covers free choice,
  mutual exclusion and sensor-steered choices.
* A transition with **two shared inputs** cannot tell which place offered, so it
  keeps every offer and can still stall (dining philosophers, `efc-pair`) unless
  the net is restructured or the grouped-offer module is used.
* **Legal grants with grouped offers** (`edg_pn_choice.c`) — every grant
  decision is legal under the same delivery assumptions. General progress,
  starvation freedom and a bound on notification storage are not proven; the
  tests and the emulator campaign are finite evidence only.
* Round robin restricts which legal choices occur, and local place counts can
  lag the reference marking while messages travel.

Not supported: arc weights, capacities, inhibitor/read arcs, guards and source
transitions (use a token source instead).

## Related repository

[`event-driven-gates-freertos`](https://github.com/kopjakj/event-driven-gates-freertos)
holds the gate library itself, with twelve C and twelve C++ example networks.

## Contact

József Kopják — Óbuda University, Kandó Kálmán Faculty of Electrical
Engineering, Budapest, Hungary — <kopjak@uni-obuda.hu>
