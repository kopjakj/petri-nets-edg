# 02 — Petri net + gates: a batch mixer (sequential control)

```
                   ┌─►( FILL_A )─►[ endA ]─►( FULL_A )─┐
START─►( REQ )─►[ start ]                               ├─►[ join ]─►( MIXING )─►[ mixdone ]─►( DRAIN )─►[ endD ]─► back to IDLE
       ( IDLE )─►   │    └─►( FILL_B )─►[ endB ]─►( FULL_B )─┘
```

A classic sequential controller — fill two ingredients in parallel, mix for a
fixed time, drain — written as a Petri net and run by the Petri-net layer in
[`../../petri/`](../../petri/README.md). It shows how the **net and ordinary
gates work together**: every step ends on a *condition*, and the condition is
computed by plain block-library objects.

```
FILL_A marked ──┐
                ├─►[ AND gate ]─►(token source)─►( evA )─►[ endA ]     "level A reached while filling A"
sensor LVL_A ───┘

MIXING marked ─►[ DELAY 2 s ]─►(token source)─►( evT )─►[ mixdone ]    "2 s after mixing began"
```

| Switch | Meaning | LED | Shows |
|---|---|---|---|
| START | flip on = request one batch (requests are counted) | VALVE_A | filling A |
| LVL_A | on = level of ingredient A reached | VALVE_B | filling B |
| LVL_B | on = level of ingredient B reached | MIXER | mixing |
| EMPTY | on = tank empty | DRAIN | draining |

## Try it

1. Flip **START** on: both valves open (two parallel branches).
2. Flip **LVL_A** on: valve A closes. Flip **LVL_B** on: valve B closes, the
   mixer starts (the `join` needed both branches).
3. After 2 s the mixer stops and the drain opens (the step timer).
4. Flip **EMPTY** on: the drain closes; the controller is idle again.
5. Leave **LVL_A** on and start another batch: filling A ends *at once*, because
   its condition already holds when the step begins. That is how a level
   condition should behave, and it needs no extra code: the AND gate rises when
   the step place becomes marked.

## What it demonstrates

* fork and join (parallelism and synchronisation) in a controller;
* a step condition = an ordinary **AND gate** over the "marked" level of the
  step place and a sensor; its rising change becomes a token;
* a step timer = the ordinary **DELAY** object on the "marked" level;
* actuators driven by the "marked" levels of places.

The net is a marked graph (no place has two output transitions), so it is in
the class for which the layer is both safe and complete.

**Limits to keep in mind.** A condition is turned into a token on its rising
change, so a sensor that rises twice within one step leaves a token behind;
inputs must be debounced (the input objects are). Alternative branches chosen
by *which* condition comes first (for example "done" versus "fault") are not
in the supported class — see the manuscript, Sections 6 and 7.

## Build and run

As every other sample (see [`../BUILDING.md`](../BUILDING.md)):

```bash
cmake -G Ninja -B build-sim -DEDG_TARGET=sim && ninja -C build-sim   # RP2040, Wokwi
cmake -G Ninja -B build-hw  -DEDG_TARGET=hw  && ninja -C build-hw    # RP2350, Pico 2 W
```

35 tasks and 35 queues: 4 inputs, 3 gates, 1 delay, 5 token sources,
12 places, 6 transitions, 4 actuators.

## Status

* Builds without warnings for both targets (Pico SDK 2.3.1, Arm GCC 13.2,
  checked 2026-11-16).
* Run on an **emulated** RP2040 (rp2040js) through two batches with scripted
  switch flips: 12 firings, all legal, and the four LEDs as expected at nine
  probe times — see [`../../petri/test/emu/run_mixer.js`](../../petri/test/emu/run_mixer.js)
  and `../../petri/test/results/emu_mixer_trace.txt`.
* `diagram.json` has **not yet been opened in Wokwi**, and the sample has
  **not yet been run on a physical board**.
