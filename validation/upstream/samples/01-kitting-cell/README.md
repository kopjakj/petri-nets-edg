# 01 — Petri net on event-driven gates: a kitting cell

```
PART ─►(src)─►( IN )─┬─►[ toA ]─►( QA )─►[ workA 1 s ]─►( RA )─┐
                     │                                         ├─►[ join ]─►( DONE )─┐
                     └─►[ toB ]─►( QB )─►[ workB 2 s ]─►( RB )─┘                     ├─►[ ship ]
SHIP ─►(src)─►( TAKE )───────────────────────────────────────────────────────────────┘

( ) place     [ ] transition     (src) token source
```

The first sample that is **not a gate network but a Petri net**, executed by the
Petri-net layer in [`../../petri/`](../../petri/README.md) on top of the
unmodified block library.

* Flip **PART** on: one part enters the cell (flip it off and on again for the next).
* Parts go to lane A and lane B **in turn** — a free choice at place `IN`.
* Lane A works for 1 s, lane B for 2 s (transitions with a firing duration).
* One part from each lane is **joined** into a kit (`DONE`).
* Flip **SHIP** on: one finished kit leaves (`ship` needs `DONE` *and* `TAKE`).

| LED | shows |
|---|---|
| IN | parts waiting to be dispatched |
| QA / QB | lane A / lane B has work queued |
| DONE | at least one finished kit is waiting |

## What it demonstrates

| Petri net | Event-driven gates |
|---|---|
| place | a level gate: its distance descriptor **is** the marking (`true` = +1 token, `false` change = −1) |
| transition | an AND gate: its distance descriptor counts the input places not yet offering a token; it fires at `d == 0` |
| conflict | a place offers **one** token to **one** output transition at a time (rotating) |
| "place is marked" | the ordinary gate output — here it drives four ordinary actuator objects |
| external event | a token source: the rising change of an ordinary input object |

`IN` and `TAKE` are unbounded counters: flip PART five times quickly and five
parts are queued. No latch network can do that.

## Build and run

Exactly as every other sample (see [`../BUILDING.md`](../BUILDING.md)):

```bash
cmake -G Ninja -B build-sim -DEDG_TARGET=sim && ninja -C build-sim   # RP2040, Wokwi
cmake -G Ninja -B build-hw  -DEDG_TARGET=hw  && ninja -C build-hw    # RP2350, Pico 2 W
```

Serial trace (UART0, 115200 baud): `+1` / `-1` are token moves with the new
marking, `fire` is a transition firing, `token` is a source.

```
[PART   ] in =1 (chg)
[arrive ] token
[IN     ] +1  M=1
[toA    ] fire
[IN     ] -1  M=0
[QA     ] +1  M=1
[workA  ] fire
[QA     ] -1  M=0
```

## Status

* Builds without warnings for both targets (Pico SDK 2.3.1, Arm GCC 13.2,
  checked 2026-11-15).
* Run on an **emulated** RP2040 (rp2040js) with scripted switch flips; every
  firing in the trace is legal — see
  [`../../petri/test/emu/`](../../petri/test/emu/) and
  `../../petri/test/results/emu_kitting_trace.txt`.
* `diagram.json` follows the layout conventions of the other samples but has
  **not yet been opened in Wokwi**, and the sample has **not yet been run on a
  physical board**.
