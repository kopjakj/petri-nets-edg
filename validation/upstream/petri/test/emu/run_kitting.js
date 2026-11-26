/*==============================================================================
 *  run_kitting.js  --  sample 01 (the kitting-cell Petri net) on an EMULATED
 *                      RP2040, with scripted switch flips and a trace check.
 *
 *  Loads the unmodified RP2040 firmware image of ../../../samples/01-
 *  kitting-cell into rp2040js (the instruction-level RP2040 emulator that the
 *  Wokwi simulator is built on), flips the PART and SHIP switches at scripted
 *  simulated times, records the UART trace, and then replays the trace in a
 *  reference token game: every "fire" line must be a legal firing.
 *
 *  usage:  node run_kitting.js <firmware.bin> <bootrom.js> [seconds]
 *     firmware.bin : arm-none-eabi-objcopy -O binary edg01_kitting_cell.elf firmware.bin
 *     bootrom.js   : the RP2040 B1 boot ROM as a CommonJS module exporting
 *                    `bootromB1` (see README.md for how to obtain it)
 *============================================================================*/
const fs = require('fs');
const { Simulator } = require('rp2040js');

const [,, binPath, romPath, secArg] = process.argv;
if (!binPath || !romPath) { console.error('usage: node run_kitting.js <firmware.bin> <bootrom.js> [seconds]'); process.exit(2); }
const { bootromB1 } = require(require('path').resolve(romPath));
const END_S = Number(secArg || 12);

const sim = new Simulator();                 /* used only as a container: we step it ourselves */
const clock = sim.clock;
const mcu = sim.rp2040;
mcu.logger = { debug() {}, info() {}, warn() {}, error() {} };   /* silence emulator chatter */
mcu.loadBootrom(bootromB1);
mcu.flash.set(fs.readFileSync(binPath), 0);
mcu.core.PC = 0x10000000;

const PIN = { PART: 14, SHIP: 15, IN: 16, QA: 17, QB: 18, DONE: 19 };
mcu.gpio[PIN.PART].setInputValue(true);      /* switches rest high (pull-up, active low) */
mcu.gpio[PIN.SHIP].setInputValue(true);

/* scripted stimulus: [simulated seconds, pin, level]  (low = switch on) */
const script = [
  [1.0, PIN.PART, false], [1.5, PIN.PART, true],     /* part 1 */
  [2.0, PIN.PART, false], [2.5, PIN.PART, true],     /* part 2 */
  [3.0, PIN.PART, false], [3.5, PIN.PART, true],     /* part 3 */
  [6.0, PIN.SHIP, false], [6.5, PIN.SHIP, true],     /* ship the first kit */
  [7.0, PIN.PART, false], [7.5, PIN.PART, true],     /* part 4 */
];

let uart = '';
const lines = [];
mcu.uart[0].onByte = (b) => {
  if (b === 10) { lines.push({ t: clock.nanos / 1e9, s: uart.replace(/\r$/, '') }); uart = ''; }
  else uart += String.fromCharCode(b);
};

const cycleNanos = 1e9 / 125000000;
let next = 0;
while (clock.nanos < END_S * 1e9) {
  while (next < script.length && clock.nanos >= script[next][0] * 1e9) {
    mcu.gpio[script[next][1]].setInputValue(script[next][2]); next++;
  }
  if (mcu.core.waiting) {
    /* the core sleeps (tickless idle): jump to the next timer alarm, but never past the next stimulus */
    let d = clock.nanosToNextAlarm;
    if (next < script.length) d = Math.min(d, Math.max(1, script[next][0] * 1e9 - clock.nanos));
    if (!(d > 0) || !isFinite(d)) d = 1e6;
    clock.tick(d);
    /* On the hardware a SysTick exception ends WFI. The emulator's stock run loop only
     * notices it when an instruction is executed, which a sleeping core never does, so
     * the check is made here. */
    if (mcu.core.interruptsUpdated && mcu.core.checkForInterrupts()) mcu.core.waiting = false;
  } else {
    clock.tick(mcu.core.executeInstruction() * cycleNanos);
  }
}

/* --- print the trace ------------------------------------------------------- */
for (const l of lines) console.log(`${l.t.toFixed(3).padStart(8)}  ${l.s}`);

/* --- replay it in the reference token game ---------------------------------- */
const net = {
  toA:   { pre: ['IN'],           post: ['QA'] },
  toB:   { pre: ['IN'],           post: ['QB'] },
  workA: { pre: ['QA'],           post: ['RA'] },
  workB: { pre: ['QB'],           post: ['RB'] },
  join:  { pre: ['RA', 'RB'],     post: ['DONE'] },
  ship:  { pre: ['DONE', 'TAKE'], post: [] },
};
const source = { arrive: 'IN', order: 'TAKE' };
const M = { IN: 0, QA: 0, QB: 0, RA: 0, RB: 0, DONE: 0, TAKE: 0 };
let fires = 0, illegal = 0, anomalies = 0; const seq = [];
for (const { s } of lines) {
  if (s.includes('!!')) anomalies++;
  let m = s.match(/^\[(\w+)\s*\] token$/);
  if (m && source[m[1]]) { M[source[m[1]]]++; continue; }
  m = s.match(/^\[(\w+)\s*\] fire$/);
  if (m && net[m[1]]) {
    const t = net[m[1]]; fires++; seq.push(m[1]);
    if (!t.pre.every((p) => M[p] > 0)) illegal++;
    t.pre.forEach((p) => M[p]--); t.post.forEach((p) => M[p]++);
  }
}
const led = (n) => (mcu.gpio[n].outputValue ? 1 : 0);
console.log('\n--- check -------------------------------------------------------');
console.log(`simulated time      : ${END_S} s, ${lines.length} trace lines`);
console.log(`firing sequence     : ${seq.join(' ')}`);
console.log(`firings             : ${fires}, illegal: ${illegal}, protocol anomalies: ${anomalies}`);
console.log(`final marking (ref) : ${JSON.stringify(M)}`);
console.log(`final LEDs          : IN=${led(PIN.IN)} QA=${led(PIN.QA)} QB=${led(PIN.QB)} DONE=${led(PIN.DONE)}`);
const ledsOk = led(PIN.IN) === (M.IN > 0 ? 1 : 0) && led(PIN.QA) === (M.QA > 0 ? 1 : 0) &&
               led(PIN.QB) === (M.QB > 0 ? 1 : 0) && led(PIN.DONE) === (M.DONE > 0 ? 1 : 0);
console.log(`LEDs match marking  : ${ledsOk ? 'yes' : 'NO'}`);
process.exit(illegal || anomalies || !ledsOk || fires === 0 ? 1 : 0);
