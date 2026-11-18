# Petri nets on Event-Driven Gates (EDG-PN)

Source code of the manuscript **“Running Petri Nets on Event-Driven Software
Gates”** (MDPI *Eng*, submitted).

Ordinary place/transition nets are executed by the unmodified Event-Driven Gates
(EDG) library: a **place** is a level gate whose distance descriptor `d` is the
marking, a **token** is a change message, and a **transition** is an AND gate that
fires when `d == 0`. The only addition is a *place-offer rule* that keeps the
token game legal for every ordinary net.

| Petri net | Event-driven gates |
|---|---|
| place `p` | level gate (one task, one queue) |
| marking `M(p)` | the distance descriptor `d` of that gate |
| token deposited | message `(true, ·)` → `d++` |
| token withdrawn | message `(false, chg)` → `d--` |
| transition `t` | AND gate; `d` = input places not yet offering a token |
| “place is marked” | the ordinary gate output `d > 0` |

## Layout

| Path | Contents |
|---|---|
| [`petri/`](petri/README.md) | The Petri-net layer (`edg_pn.c`, `edg_pn.h`), its tests and the recorded results quoted in the manuscript |
| [`samples/common/`](samples/common/README.md) | The EDG gate library (`edg.c`, `edg.h`), the FreeRTOS configuration and the host-test harness |
| [`samples/01-kitting-cell/`](samples/01-kitting-cell/README.md) | Sample 01: kitting cell — rotating dispatch, timed activities, pairwise join |
| [`samples/02-batch-mixer/`](samples/02-batch-mixer/README.md) | Sample 02: batch mixer — sequential control with fork/join |
| [`samples/BUILDING.md`](samples/BUILDING.md) | Toolchain, Pico SDK, Wokwi and hardware build steps |

## Quick start — host tests (no hardware)

```bash
cd petri/test
make                    # unit tests + a few nets + a naive counterexample (seconds)
./run_explore.sh        # the full exploration table (~15 min, ~4 GB RAM)
python3 make_tables.py  # regenerates the manuscript tables from results/
```

`test/results/` holds the output files every number in the manuscript comes
from; `make_tables.py` prints the same rows and totals from them.

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
./run_freertos.sh       # 24 runs of 1,000,000 firings (~20 min)
./run_examples.sh       # the three application examples (~20 min, ~4 GB RAM)
```

The emulator scripts (`test/emu/run_kitting.js`, `test/emu/run_mixer.js`) need
Node.js and `rp2040js`; see [`petri/README.md`](petri/README.md).

## What the code guarantees

* **Safe for every ordinary net** — every firing is legal under every task
  interleaving.
* **Complete for conflict-free and free-choice nets** — the network stops only
  in a dead marking.
* Other nets (mutual exclusion, dining philosophers) stay safe, but a token can
  be *parked* at a transition that cannot use it: the limit of anonymous
  messages, analysed in the manuscript.

Not supported: arc weights, capacities, inhibitor/read arcs, guards and source
transitions.

## Related repository

[`event-driven-gates-freertos`](https://github.com/kopjakj/event-driven-gates-freertos)
holds the gate library itself, with twelve C and twelve C++ example networks.

## Contact

József Kopják — Óbuda University, Kandó Kálmán Faculty of Electrical
Engineering, Budapest, Hungary — <kopjak@uni-obuda.hu>
