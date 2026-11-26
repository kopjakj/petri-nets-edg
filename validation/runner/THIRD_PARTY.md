# Emulator inputs and provenance

- The RP2040 B1 boot-ROM array is **not bundled** here. Run
  `python3 results_added/emulator/fetch_bootrom.py` to download the
  [rp2040js v1.4.2 demo](https://github.com/wokwi/rp2040js/blob/v1.4.2/demo/bootrom.ts)
  source and convert its export to a CommonJS `bootrom.cjs` module. The
  upstream file identifies RP2040 boot-ROM revision
  `00a4a19114195e20fb817bdfbca1165e157eef37`. Its underlying
  [pico-bootrom licensing](https://github.com/raspberrypi/pico-bootrom)
  includes special terms for math-library portions; check those terms
  before using the ROM. Fetching it is an explicit local step rather than
  redistributing the ROM in this upload.
- `package.json` pins `rp2040js` at version 1.4.2;
  `package-lock.json` records its registry integrity hash. Install locally
  with `npm ci --prefix results_added/emulator`.
- `../firmware/binaries/*.bin` are the four archived RP2040 images, rebuilt
  from `../firmware/main.c` and the unmodified EDG library in this project.
  The corresponding raw UART traces are `emu_*.trace.gz`. Images are included
  so the archived simulation can be rerun without the Arm build toolchain.

These are emulator runs, not measurements on a physical RP2040 board. The
crossing and car-park implementation-state searches were capped and do not
prove exhaustive coverage.
