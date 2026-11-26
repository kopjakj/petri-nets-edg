# common — the event-driven-gates block library

Every sample shares this small library, exactly as the Node-RED implementation
(`gen_flows.py`) shares one block library across all its demo flows. Each sample
adds this directory with `add_subdirectory` and links the `edg` target, so each
sample's `main.c` is pure *wiring*.

| File | Role |
|---|---|
| `edg.h` / `edg.c` | The block library: the `edg_event_t` message, the object task bodies (`edg_input_task`, `edg_gate_task`, `edg_actuator_task`, `edg_delay_task`), the pure gate state machine (`edg_gate_step`), helpers (`edg_set_logic`, `edg_sym_mask`, `edg_wire`), and the kernel hooks. |
| `FreeRTOSConfig.h` | FreeRTOS configuration shared by both targets (single-core). RP2350-only options are bracketed by `PICO_RP2350`. |
| `pico_sdk_import.cmake` | Locates the Pico SDK (`PICO_SDK_PATH`). |
| `FreeRTOS_Kernel_import.cmake` | Locates the FreeRTOS kernel and selects the port matching `PICO_PLATFORM` (RP2040 or RP2350_ARM_NTZ). |
| `CMakeLists.txt` | Defines the `edg` static library target. |
| `test/` | Host-side unit tests / simulations of the gate logic (no hardware). |

## The object model in one page

| Object | Task | Behaviour |
|---|---|---|
| Input (active) | `edg_input_task` | **Interrupt-driven** — a GPIO edge wakes it (never polls); sends one `init` then `change` messages. `init_mode` chooses whether the `init` goes out at startup (`EDG_INIT_EAGER`) or on the first edge (`EDG_INIT_ON_EDGE`). |
| Gate (passive) | `edg_gate_task` | Blocks on its input queue; runs `edg_gate_step`; emits on output change. |
| Actuator (passive) | `edg_actuator_task` | Blocks on its input queue; drives an LED GPIO. |
| Delay (passive) | `edg_delay_task` | Forwards each message after a fixed delay (used for the clock). |

### Inputs are interrupt-driven

An input object does not poll. The pin's rising and falling edges raise the
hardware GPIO interrupt (`IO_IRQ_BANK0`); one shared ISR (`edg_gpio_callback` in
`edg.c`) maps the pin to its owning task and wakes it with a task notification.
The task then **masks that pin's own interrupt**, waits `debounce_ms` for the line
to settle, reads the level, discards any notifications the bounce left pending,
re-arms the interrupt, and emits a message only if the level actually changed.
While idle the task is blocked and costs nothing — which, with
`configUSE_TICKLESS_IDLE`, lets the core halt between events. (The GPIO interrupt
is a real NVIC interrupt, so it wakes the halted core; see `../BUILDING.md`.)

Masking the interrupt during the settle window is what makes the debounce work.
A real contact — and a Wokwi switch, whose bounce is **on by default** — does not
flip once per toggle: it opens and closes tens or hundreds of times as it
settles. Each of those edges is its own interrupt, so leaving the pin armed while
debouncing lets them pile up as notifications and re-trigger the task, spraying
the trace with `in=1 / in=0 / in=1 …` chatter for a single physical flip. Masking
absorbs the whole burst into one clean transition. (The simulator diagrams also
set the switches' `bounce` attribute to `0`, since a didactic gate input is a
clean logic source; the firmware masking is what protects real hardware.)

The one `init` message is sent according to `edg_input_t.init_mode`:

| `init_mode` | At startup | On the first edge |
|---|---|---|
| `EDG_INIT_EAGER` (default) | read the pin, send `init` with that level | send a `change` |
| `EDG_INIT_ON_EDGE` | send nothing | send `init` with the level read then |

`01-basic-and-gate` exposes both with `-DEDG_INIT_ON_EDGE=ON`.

A **gate** is configured by `kind` (`EDG_LEVEL` / `EDG_PARITY` / `EDG_SYMMETRIC`),
two inverter flags, and — for symmetric gates — a `sym_mask`. `edg_set_logic()`
turns a named function (`EDG_AND`, `EDG_NAND`, `EDG_OR`, `EDG_NOR`, `EDG_NOT`,
`EDG_BUF`) into the right flags. See `../../ANALYSIS.md` for the theory.

## Kernel hooks

`edg.c` also supplies the four FreeRTOS hooks every sample links:

| Hook | Purpose |
|---|---|
| `vApplicationStackOverflowHook` | `panic()`s with the task name, instead of hanging silently. |
| `vApplicationMallocFailedHook` | `panic()`s when the heap runs out creating a task or queue. |

No idle or tick hook is used: `configUSE_TICKLESS_IDLE` already halts the core in
the idle task (`vPortSuppressTicksAndSleep`, a `wfi`), which is both better and
what the known-good Wokwi FreeRTOS project does.
