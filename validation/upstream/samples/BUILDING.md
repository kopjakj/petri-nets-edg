# Building & running the samples — step-by-step

Every sample here is an independent CMake project built with **FreeRTOS** and the
**Pico C/C++ SDK**. Each one builds for **two targets** from a single source
tree, selected with `-DEDG_TARGET`:

* **`sim`** — RP2040, Raspberry Pi Pico. This is what the **Wokwi** simulator
  runs; Wokwi emulates no other MCU.
* **`hw`** — RP2350, Raspberry Pi Pico 2 W (Arm Cortex-M33). This is what you
  flash.

The same `main.c`, `edg.c`, `FreeRTOSConfig.h` and pin numbers serve both. The
steps are the same for all twelve samples; only the directory and firmware name
change.

This guide starts from nothing and ends with the sample running in the Wokwi
simulator and/or on a real board. Commands are shown for Linux/macOS `bash`;
Windows notes are called out where they differ.

---

## Step 1 — Install the toolchain

You need four things on your machine:

1. **Arm GNU Toolchain** (`arm-none-eabi-gcc`), version 13.x or newer.
2. **CMake ≥ 3.13** and **Ninja** (or Make).
3. **Git**.
4. **Python 3** (the SDK uses it while building).

### Linux (Debian/Ubuntu)
```bash
sudo apt update
sudo apt install cmake ninja-build git python3 build-essential
# Arm toolchain: either from apt...
sudo apt install gcc-arm-none-eabi
# ...or download the official Arm GNU Toolchain (arm-none-eabi) and add its
# bin/ directory to PATH if you want a newer version.
```

### macOS (Homebrew)
```bash
brew install cmake ninja git python
brew install --cask gcc-arm-embedded    # arm-none-eabi toolchain
```

### Windows
Install [CMake], [Ninja], [Git for Windows], [Python 3], and the
[Arm GNU Toolchain] (arm-none-eabi) — or simply use the official
**Raspberry Pi Pico VS Code extension**, which bundles all of them. Run the
commands below from the *"Pico – Developer Command Prompt"* or a Git Bash shell.

[CMake]: https://cmake.org/download/
[Ninja]: https://github.com/ninja-build/ninja/releases
[Git for Windows]: https://git-scm.com/download/win
[Python 3]: https://www.python.org/downloads/
[Arm GNU Toolchain]: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

**Check it worked:**
```bash
arm-none-eabi-gcc --version   # expect 13.x or newer
cmake --version               # expect 3.13+
ninja --version
```

---

## Step 2 — Get the Pico SDK and FreeRTOS kernel (git submodules)

Both are **submodules of this repository**, under `pico/`. Clone with them:

```bash
git clone --recurse-submodules <this-repo> event-driven-gates
```

Already cloned without `--recurse-submodules`? Fetch them in place:

```bash
cd event-driven-gates
git submodule update --init
```

That is all the setup there is. The resulting layout:

```
event-driven-gates/       ← this repository
  pico/
    pico-sdk/             ← submodule, pinned at tag 2.3.0 (RP2350 needs 2.1.0+)
    FreeRTOS-Kernel/      ← submodule, the Raspberry Pi fork (carries the
                            RP2350 Arm port the samples select)
    picotool/             ← created automatically by the first build (Step 5)
    riscv-toolchain/      ← only for the RISC-V appendix; download it yourself
```

Neither submodule needs `--recursive`. The SDK's own submodules are for TinyUSB,
WiFi and Bluetooth; these samples use UART serial and no radio, so the SDK's
`cyw43-driver / btstack / lwip submodule has not been initialized` notes during
an `EDG_TARGET=hw` build are expected and harmless.

The sample `CMakeLists.txt` files find the SDK and kernel by looking first for a
`pico/` folder **next to** the repository and then for one **inside** it, so the
submodule layout above needs no configuration at all (Steps 3–4 are optional).
Read Step 5 (nothing to install) and build (Step 6).

---

## Step 3 — (Optional) Use a different SDK / kernel location

If your SDK and kernel live somewhere else, point the build at them with
environment variables (they **override** the `../pico/` default):

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel
# Only if arm-none-eabi-gcc is NOT already on your PATH:
# export PICO_TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain
```

Add them to `~/.bashrc` / `~/.zshrc` to make them permanent. On Windows
(PowerShell): `$env:PICO_SDK_PATH = "C:\path\to\pico-sdk"`. You can also pass
them on the CMake command line: `-DPICO_SDK_PATH=... -DFREERTOS_KERNEL_PATH=...`.

Precedence (highest first): a `-D` value → an environment variable → the
built-in `../pico/` default.

> You never set `PICO_BOARD` or `PICO_PLATFORM` yourself — `-DEDG_TARGET=sim|hw`
> derives both (Step 6).

---

## Step 4 — Set the toolchain path if needed

Nothing to do here **if `arm-none-eabi-gcc` is already on your `PATH`** (check
with `arm-none-eabi-gcc --version`). Otherwise:

```bash
export PICO_TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain   # dir containing bin/arm-none-eabi-gcc
```

---

## Step 5 — picotool: nothing to install

**Do not install picotool.** The build handles it. This step exists only to
explain what you will see scroll past.

`picotool` is a **build-time tool**, not something you run. The Pico SDK 2.x uses
it to turn the linked `.elf` into the `.uf2` — the only format Wokwi loads and the
only format the BOOTSEL drive accepts. There is no longer an `elf2uf2` fallback:
build with `-DPICO_NO_PICOTOOL=1` and you get an `.elf`, a `.bin` and a `.hex`,
and **no `.uf2` at all**. So picotool is required, and it cannot be removed.

What it is *not* required for: flashing. Drag-and-drop of the `.uf2` onto the
BOOTSEL drive needs no tool whatsoever (Step 8). You never invoke picotool
yourself.

If picotool is not already on your system, the SDK clones and compiles it — and
each sample's `CMakeLists.txt` points it at **one shared directory**,
`../pico/picotool`, so this happens exactly once for all 12 samples × 2 targets
rather than 24 times. Expect this on the very first build only:

```
Downloading Picotool
```

That build takes about half a minute longer. Every build after it, in any sample
and either target, reuses the same binary and is fast.

Override the location if you like — an already-installed picotool wins over
everything, then `-DPICOTOOL_FETCH_FROM_GIT_PATH=...`, then the environment
variable of the same name, then the `../pico/picotool` default.

> If you previously followed advice to `sudo apt install picotool` and got
> **`Unable to locate package picotool`**: most distros do not package it (only
> Ubuntu 24.04+ / Debian 13+ do). Ignore it — nothing needs installing.

> **Windows / VS Code:** the Raspberry Pi Pico VS Code extension bundles a
> matching picotool, which the SDK will find and use.

---

## Step 6 — Build a sample (two targets, one source tree)

Every sample builds for **two different chips** from exactly the same `main.c`,
`edg.c` and `FreeRTOSConfig.h`. Pick one with `-DEDG_TARGET`:

| `-DEDG_TARGET=` | Chip | Board | `PICO_PLATFORM` | FreeRTOS port | Purpose |
|---|---|---|---|---|---|
| `sim` *(default)* | RP2040 | Raspberry Pi Pico | `rp2040` | `.../GCC/RP2040` | the **Wokwi simulator** |
| `hw` | RP2350 | Raspberry Pi Pico 2 **W** | `rp2350-arm-s` | `.../GCC/RP2350_ARM_NTZ` | **real hardware** (Arm Cortex-M33) |

Why two: **Wokwi emulates the RP2040 only.** Its
[supported-hardware list](https://docs.wokwi.com/getting-started/supported-hardware)
names no other MCU, and its online Pico-SDK builder compiles for
`PICO_PLATFORM=rp2040` regardless of the board in `diagram.json`. So every
`diagram.json` in this tree uses the `wokwi-pi-pico` board part, and every
`wokwi.toml` points at `build-sim/`. The board you actually flash is a Pico 2 W.

Build them into **separate directories** — the two chips' object files must
never mix:

```bash
cd 01-basic-and-gate

# the simulator build (RP2040)
cmake -G Ninja -B build-sim -DCMAKE_BUILD_TYPE=Release -DEDG_TARGET=sim
ninja -C build-sim

# the hardware build (RP2350, Pico 2 W)
cmake -G Ninja -B build-hw  -DCMAKE_BUILD_TYPE=Release -DEDG_TARGET=hw
ninja -C build-hw
```

CMake echoes the choice so you can be sure which one you got:

```
-- EDG_TARGET=sim -> PICO_PLATFORM=rp2040 PICO_BOARD=pico
```

`EDG_TARGET` is cached. To switch an existing directory, `rm -rf` it first — or
just keep both, as above.

* The **first** build of the first sample also compiles `picotool` (Step 5),
  adding roughly half a minute. Every build after that reuses it.
* A clean build finishes with `Linking CXX executable edg01_basic_and.elf`.

Each build directory then holds:

| File | Use |
|---|---|
| `edg01_basic_and.uf2` | the firmware — Wokwi loads it, and it drag-and-drops onto a real board |
| `edg01_basic_and.elf` | for Wokwi's `elf =` key and for a debugger. **Not flashable.** |
| `.hex`, `.bin` | alternative formats |
| `.dis` | disassembly |

Confirm which chip a `.uf2` is for — the two are not interchangeable, and the
bootloader silently refuses the wrong one:

```bash
head -c 32 build-sim/edg01_basic_and.uf2 | od -An -tx4 -j28   # e48bff56 = RP2040
head -c 32 build-hw/edg01_basic_and.uf2  | od -An -tx4 -j28   # e48bff57/59 = RP2350
```

Repeat in any other sample folder.

---

## Step 7 — Run it in Wokwi (browser simulator)

Wokwi runs the **`sim`** build. Each sample ships a `diagram.json` (a
`wokwi-pi-pico` board plus switches and LEDs) and a `wokwi.toml` already pointing
at `build-sim/…​.uf2` and `build-sim/…​.elf`.

### Option A — VS Code + Wokwi extension (recommended)
1. Install the **Wokwi Simulator** extension in VS Code.
2. Open the sample folder and build `build-sim` (Step 6).
3. Press <kbd>F1</kbd> → **"Wokwi: Start Simulator"** (or open `diagram.json` and
   click **Start**). The `wokwi.toml` loads the firmware you just built.

### Option B — wokwi.com
1. Go to <https://wokwi.com/> and start a new (blank) project.
2. Replace its `diagram.json` with this sample's `diagram.json`.
3. Upload the built `.uf2` (and `.elf`) as the firmware.

**Then:** flip the switches in the simulation and watch the LEDs. Open the
**Serial Monitor** to see every message the network processes — each object's
`init`/`change` events and the gate's distance `d`.

> Do not point Wokwi at `build-hw/`. An RP2350 image will not run on Wokwi's
> RP2040 core.

### The freeze — SDK↔FreeRTOS interop must be off

Before speed there is a correctness problem, and it is the one that matters most:
with the Raspberry Pi RP2xxx FreeRTOS port's defaults, **the scheduler freezes
Wokwi solid the moment it starts** — 0 % of real time, the tab locked up.

The port defaults `configSUPPORT_PICO_SYNC_INTEROP` and
`configSUPPORT_PICO_TIME_INTEROP` to 1. These make the SDK's own lock and sleep
primitives cooperate with FreeRTOS by rebuilding their wait/notify on the `wfe` /
`sev` instruction pair. But [rp2040js](https://github.com/wokwi/rp2040js) — the
RP2040 emulator behind Wokwi — implements **`sev` as a no-op**. A wait that
depends on a matching `sev` never wakes, and the kernel wedges.

`common/FreeRTOSConfig.h` therefore sets both to **0**, on both targets. That is
what the known-good Wokwi FreeRTOS project effectively does — it uses the generic
ARM_CM0 port, which has no such interop. For these samples nothing is lost:
objects talk through FreeRTOS queues, never SDK sync objects, and all tasks share
one priority under time slicing, so the single SDK lock behind `printf` cannot
invert.

### Simulation speed — why an RTOS is different

With the freeze fixed, there is still speed. Wokwi's Pico interprets
every instruction the emulated core retires — but a **halted** core costs it
nothing: the run loop simply advances the simulated clock to the next timer
event (`src/simulator.ts`). Simulation speed is therefore governed by one number:
**instructions retired per simulated second.**

That is why a plain Pico-SDK program feels instant and an RTOS one crawls. A bare
SDK program spends its life inside `sleep_ms()`, which arms a hardware timer
alarm and halts the core. Start the FreeRTOS scheduler, though, and the **idle
task** — which in an event-driven gate network is what runs ~99.9 % of the time,
since every object sits blocked on a queue — is an unconditional busy loop:
125 million instructions per simulated second, all of them interpreted, none of
them doing anything.

So the idle task has to halt. `common/FreeRTOSConfig.h` sets
**`configUSE_TICKLESS_IDLE 1`**. When no task is ready, the port's
`vPortSuppressTicksAndSleep()` stops SysTick, executes `wfi`, and on waking
credits the kernel with the ticks that elapsed. The core is halted for nearly the
whole run — the emulator skips it, and a real board idles at ~1 mA instead of
~20 mA.

This is exactly what the known-good Wokwi FreeRTOS project does (Tony Smith's
RP2040 template, [project 413080804206920705](https://wokwi.com/projects/413080804206920705)),
which runs at 100 % of real time. Its idle `wfi` is woken by SysTick alone, so a
plain `wfi` is all that is required; no hardware-timer workaround is needed.

There is one caveat. While asleep, ticks are *skipped* rather than counted, and
`vTaskStepTick()` credits them afterwards, so `vTaskDelay()` stays accurate.

Two more knobs, if a sample still feels sluggish:

* **Build `Release`.** The SDK defaults to it, but `-DCMAKE_BUILD_TYPE=Debug`
  compiles at `-O0` and gives the emulator several times more instructions to
  chew through.
* **Trace less.** `.trace = true` on an object prints one line per message. That
  is what the samples are *for*, but a `printf` at 115200 baud takes about
  90 µs per character of emulated UART time.

Wokwi shows a **performance meter** (percent of real time) while it runs — that
is the number to watch when changing any of this.

---

## Step 8 — Run it on real hardware (optional)

> **Copy the `.uf2`, never the `.elf`.** The BOOTSEL drive only accepts UF2
> files. Dropping an `.elf` on it is silently ignored: nothing is written, the
> board does **not** reboot, and it keeps running whatever firmware was flashed
> before — which looks exactly like "my new code changed nothing". The `.elf` is
> for debuggers (and for Wokwi's `elf =` key); it is not a flashable image.
> No `picotool` is needed to flash — drag-and-drop is enough.

Hardware is the **`hw`** build: `build-hw/`, RP2350, Raspberry Pi Pico 2 W.

1. Build it: `cmake -G Ninja -B build-hw -DEDG_TARGET=hw && ninja -C build-hw`.
2. Hold the **BOOTSEL** button while plugging the Pico 2 W into USB. It mounts as
   a USB drive named `RP2350`.
3. Copy `build-hw/edgNN_name.uf2` onto that drive. The drive disappears, the
   board reboots, and it runs immediately.
4. Wire the switches and LEDs as shown in the sample's `diagram.json` and its
   README. The pin numbers are identical on both boards:
   * **Switches** use the internal pull-ups — just connect the GPIO to GND
     through the switch (no external resistor needed).
   * **LEDs** need a series resistor (~220 Ω) from the GPIO to the LED anode;
     the cathode goes to GND.
5. **Serial output** is on **UART0** (GP0 = TX, GP1 = RX) at **115200 baud**.
   Open it with a USB-to-UART adapter and `screen`/`minicom`/`putty`. (USB-CDC
   serial is intentionally disabled — TinyUSB is not serviced under FreeRTOS in
   the simulator, where a USB `printf` would stall the tasks. If you want USB
   serial on real hardware, re-enable it in the sample's `CMakeLists.txt` with
   `pico_enable_stdio_usb(${PROJECT_NAME} 1)`.)

> The Pico 2 W's WiFi is not used, and `pico_cyw43_arch` is not linked. The board
> is chosen only because that is the hardware on the bench. Its user LED hangs
> off the CYW43 radio chip rather than a GPIO, which is why the samples drive
> plain external GPIO LEDs instead of using `PICO_DEFAULT_LED_PIN` — that macro is
> GP25 on the RP2040 Pico and does not exist at all on the Pico 2 W.

---

## Appendix — the RP2350's RISC-V cores

The RP2350 carries **two** pairs of cores: the Arm Cortex-M33 pair the `hw`
target uses, and a pair of open-hardware Hazard3 **RISC-V** cores. Only one pair
runs at a time, chosen at build time. This is not one of the two supported
targets, but it is a useful diagnostic, because the two ports take their kernel
tick from entirely different hardware: the Arm port from the Cortex-M
**SysTick**, the RISC-V port from the SIO **machine timer** (`mtime`/`mtimecmp`,
`SIO_IRQ_MTIMECMP`). If the `hw` build shows a dead tick but a RISC-V build ticks
fine, the fault is in SysTick rather than in FreeRTOS or in your code.

RISC-V needs its own bare-metal compiler; the Arm one cannot target it. Fetch a
prebuilt `riscv32-unknown-elf-gcc` from
[pico-sdk-tools](https://github.com/raspberrypi/pico-sdk-tools/releases) and
unpack it to `../pico/riscv-toolchain` (or anywhere — the path is passed
explicitly below). It is ~390 MB of binaries, it is **not** tracked in this
repository, and nothing in the two supported targets uses it.

To build it you must bypass `EDG_TARGET` and drive the SDK variables directly:

```bash
cd 01-basic-and-gate
cmake -G Ninja -B build-riscv -DCMAKE_BUILD_TYPE=Release \
      -DEDG_TARGET=hw -DPICO_PLATFORM=rp2350-riscv \
      -DPICO_TOOLCHAIN_PATH="$(cd ../../pico/riscv-toolchain && pwd)"
ninja -C build-riscv
readelf -h build-riscv/edg01_basic_and.elf | grep Machine     # -> RISC-V
```

`PICO_TOOLCHAIN_PATH` must be **absolute** — CMake resolves it against the build
directory, not your shell's working directory. The `-DPICO_PLATFORM` override
makes CMake print a warning that it is overriding the `EDG_TARGET=hw` default;
that is expected here.

Flash it exactly as in Step 8; the UF2 carries a RISC-V image ID and the
bootloader switches the chip's architecture for you. **Wokwi cannot run it.**

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `PICO_SDK_PATH ... not found` / SDK errors | Put `pico-sdk` in `../pico/` (Step 2) or set `PICO_SDK_PATH` (Step 3). |
| `FreeRTOS location was not specified` | Put `FreeRTOS-Kernel` in `../pico/` (Step 2) or set `FREERTOS_KERNEL_PATH` (Step 3). |
| `Directory ... does not contain a 'rp2350-arm-s' port` | Your FreeRTOS-Kernel is too old / not the Raspberry Pi fork. Use `https://github.com/raspberrypi/FreeRTOS-Kernel` (Step 2). |
| `arm-none-eabi-gcc: not found` | Install the Arm toolchain or set `PICO_TOOLCHAIN_PATH` (Step 1/4). |
| Flashed new firmware, board behaves exactly as before / never reboots | You copied the `.elf`, or the `.uf2` from the wrong target. Only a **`.uf2`** with the matching family ID can be dropped on the BOOTSEL drive (Step 6/8). |
| Only the startup banner prints; tasks never run / LED dead | Most likely the FreeRTOS exception handlers were only in the RAM vector table and VTOR was never honoured, so `svc 0` in `vTaskStartScheduler()` hit the SDK's default infinite loop. `configUSE_DYNAMIC_EXCEPTION_HANDLERS 0` (in `common/FreeRTOSConfig.h`) links them into the flash vector table instead — make sure you rebuilt clean. |
| LED never lights / diagram fails to load | A connection names a pin the board does not have. `wokwi-pi-pico` has **no** pin called `GND` — only `GND.1`…`GND.8`. One bad pin reference invalidates the whole `diagram.json`. |
| Nothing runs in Wokwi at all | Wokwi emulates the **RP2040**. Point it at `build-sim/`, not `build-hw/`. |
| The simulation runs far slower than real time once the scheduler starts | The FreeRTOS idle task is a busy loop and the emulator interprets every instruction of it (Step 7, *Simulation speed*). Check tickless idle survived into your image: `arm-none-eabi-objdump -d build-sim/*.elf \| awk '/<prvIdleTask>:/,/^$/' \| grep vPortSuppressTicksAndSleep` must match. Also confirm you did not build `Debug`. |
| Tick never fires on `hw`, and you suspect SysTick | Build the same sample for the RISC-V cores (Appendix). They tick off the SIO machine timer instead, so a working RISC-V build isolates the fault to SysTick. |
| `cyw43-driver / btstack / lwip submodule has not been initialized` | Harmless. `EDG_TARGET=hw` selects the Pico 2 **W** board header, so the SDK notes that wireless is unavailable. No sample uses WiFi and `pico_cyw43_arch` is never linked. |
| Changed `EDG_TARGET` but the build ignores it | `PICO_PLATFORM` and `PICO_BOARD` are cached by the SDK. CMake warns about the mismatch; `rm -rf build-sim build-hw` and re-run. |
| `apt`: `Unable to locate package picotool` | Nothing to install — the build fetches it. Ignore (Step 5). |
| `Downloading Picotool` on the first build | Expected, once. It is compiled into the shared `../pico/picotool` and reused by every later build (Step 5). |
| `Downloading Picotool` on **every** build | The shared directory is not being reused. Check that `../pico/` exists next to (or inside) the repo, or set `PICOTOOL_FETCH_FROM_GIT_PATH` yourself (Step 5). |
| No `.uf2` in the build directory, only `.elf`/`.bin`/`.hex` | You built with `-DPICO_NO_PICOTOOL=1`. The `.uf2` cannot be produced without picotool, and nothing else is flashable (Step 5). |
| No serial output in Wokwi | Open the **Serial Monitor** panel; output also appears once the 300 ms startup banner delay elapses. |
| Rebuild from scratch | `rm -rf build-sim build-hw` then repeat Step 6. |

---

## One-liner recap

With the SDK and kernel in `../pico/` (Step 2), no env vars are needed:

```bash
cd 01-basic-and-gate

# simulator (RP2040) -- then: Wokwi "Start Simulator"
cmake -G Ninja -B build-sim -DCMAKE_BUILD_TYPE=Release -DEDG_TARGET=sim && ninja -C build-sim

# hardware (RP2350) -- then: drag build-hw/edg01_basic_and.uf2 onto the Pico 2 W
cmake -G Ninja -B build-hw  -DCMAKE_BUILD_TYPE=Release -DEDG_TARGET=hw  && ninja -C build-hw
```

Using a custom SDK/kernel location instead? Prefix with:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel
```
