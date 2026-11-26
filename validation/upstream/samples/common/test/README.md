# Host-side verification of the gate logic

These tests compile the **real** `../edg.c` on your PC (no Pico SDK, no
hardware) against tiny stub headers in `inc/`, and check the gate behaviour
directly. They are what backs the "verified in a host simulation" notes in the
sample READMEs.

```bash
make          # builds and runs all four
make clean
```

| File | Checks |
|---|---|
| `test.c` | Exhaustive truth tables: AND/OR/NAND/NOR, NOT/BUF, XOR/XNOR (2- and 3-input), majority(3), and dynamic change-notification toggling. |
| `fb.c` | Feedback networks: the SR latch (defined power-up, set/reset/hold, and the metastable cold-start storm) and the gated D latch (transparent/hold). |
| `alu_sim.c` | The full 1-bit ALU network against a reference ALU across all 32 input combinations. |
| `interlock_sim.c` | The safety-interlock network through a complete operating sequence (start, seal-in, stop, E-stop, guard-open, over-temp). |

The stub headers in `inc/` provide just enough of `FreeRTOS.h`, `queue.h`,
`task.h`, and `pico/stdlib.h` for `edg.c` to compile natively; the gate decision
logic (`edg_gate_step`) uses none of them, so the tests exercise the exact code
that runs on the Pico 2.
