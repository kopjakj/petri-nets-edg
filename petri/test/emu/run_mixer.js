/*==============================================================================
 *  run_mixer.js  --  sample 02 (the batch mixer: Petri net + gates) on an EMULATED
 *                      RP2040, with scripted switch flips and a trace check.
 *
 *  Loads the unmodified RP2040 firmware image of ../../../samples/02-
 *  batch-mixer into rp2040js (the instruction-level RP2040 emulator that the
 *  Wokwi simulator is built on), flips the START, LVL_A, LVL_B and EMPTY switches at scripted
 *  simulated times, records the UART trace, and then replays the trace in a
 *  reference token game: every "fire" line must be a legal firing.
 *
 *  usage:  node run_mixer.js <firmware.bin> <bootrom.js> [seconds]
 *     firmware.bin : arm-none-eabi-objcopy -O binary edg02_batch_mixer.elf firmware.bin
 *     bootrom.js   : the RP2040 B1 boot ROM as a CommonJS module exporting
 *                    `bootromB1` (see README.md for how to obtain it)
 *============================================================================*/
const fs = require('fs');
const { Simulator } = require('rp2040js');

const [,, binPath, romPath, secArg] = process.argv;
if (!binPath || !romPath) { console.error('usage: node run_mixer.js <firmware.bin> <bootrom.js> [seconds]'); process.exit(2); }
const { bootromB1 } = require(require('path').resolve(romPath));
const END_S = Number(secArg || 15);

const sim = new Simulator();                 /* used only as a container: we step it ourselves */
const clock = sim.clock;
const mcu = sim.rp2040;
mcu.logger = { debug() {}, info() {}, warn() {}, error() {} };   /* silence emulator chatter */
mcu.loadBootrom(bootromB1);
mcu.flash.set(fs.readFileSync(binPath), 0);
mcu.core.PC = 0x10000000;

const PIN = { START: 2, LVL_A: 3, LVL_B: 4, EMPTY: 5, VALVE_A: 16, VALVE_B: 17, MIXER: 18, DRAIN: 19 };
for (const p of [PIN.START, PIN.LVL_A, PIN.LVL_B, PIN.EMPTY]) mcu.gpio[p].setInputValue(true);  /* rest high */

/* scripted stimulus: [simulated seconds, pin, level]  (low = switch on) */
const script = [
  /* batch 1: every condition becomes true while its step is active */
  [1.0, PIN.START, false],                           /* request a batch            */
  [2.0, PIN.LVL_A, false],                           /* level A reached            */
  [3.0, PIN.LVL_B, false],                           /* level B reached -> mixing  */
  [6.0, PIN.EMPTY, false],                           /* tank empty (after mixing)  */
  [6.5, PIN.START, true], [7.0, PIN.LVL_B, true], [7.2, PIN.EMPTY, true],
  /* batch 2: LVL_A is STILL ON when filling A starts -> that step ends at once */
  [9.0, PIN.START, false],
  [10.0, PIN.LVL_B, false],
  [13.0, PIN.EMPTY, false],
];
/* LED states to sample: [simulated seconds, expected VALVE_A VALVE_B MIXER DRAIN] */
const probes = [
  [1.5, '1100'], [2.5, '0100'], [4.0, '0010'], [5.7, '0001'], [7.5, '0000'],
  [9.5, '0100'], [11.0, '0010'], [12.7, '0001'], [14.5, '0000'],
];
const leds = () => [PIN.VALVE_A, PIN.VALVE_B, PIN.MIXER, PIN.DRAIN].map((p) => (mcu.gpio[p].outputValue ? 1 : 0)).join('');
const probed = []; let np = 0;

let uart = '';
const lines = [];
mcu.uart[0].onByte = (b) => {
  if (b === 10) { lines.push({ t: clock.nanos / 1e9, s: uart.replace(/\r$/, '') }); uart = ''; }
  else uart += String.fromCharCode(b);
};

const cycleNanos = 1e9 / 125000000;
let next = 0;
while (clock.nanos < END_S * 1e9) {
  while (np < probes.length && clock.nanos >= probes[np][0] * 1e9) { probed.push([probes[np][0], probes[np][1], leds()]); np++; }
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
  start:   { pre: ['IDLE', 'REQ'],      post: ['FILL_A', 'FILL_B'] },
  endA:    { pre: ['FILL_A', 'evA'],    post: ['FULL_A'] },
  endB:    { pre: ['FILL_B', 'evB'],    post: ['FULL_B'] },
  join:    { pre: ['FULL_A', 'FULL_B'], post: ['MIXING'] },
  mixdone: { pre: ['MIXING', 'evT'],    post: ['DRAIN'] },
  endD:    { pre: ['DRAIN', 'evE'],     post: ['IDLE'] },
};
const source = { request: 'REQ', evA: 'evA', evB: 'evB', evT: 'evT', evE: 'evE' };
const M = { IDLE: 1, REQ: 0, FILL_A: 0, FILL_B: 0, FULL_A: 0, FULL_B: 0, MIXING: 0, DRAIN: 0, evA: 0, evB: 0, evT: 0, evE: 0 };
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
const ledsOk = probed.length === probes.length && probed.every(([, want, got]) => want === got);
const marked = Object.keys(M).filter((p) => M[p] > 0).map((p) => `${p}=${M[p]}`).join(' ');
console.log('\n--- check -------------------------------------------------------');
console.log(`simulated time      : ${END_S} s, ${lines.length} trace lines`);
console.log(`firing sequence     : ${seq.join(' ')}`);
console.log(`firings             : ${fires}, illegal: ${illegal}, protocol anomalies: ${anomalies}`);
console.log(`final marking (ref) : ${marked}`);
console.log('LED probes (t: VALVE_A VALVE_B MIXER DRAIN, expected/observed):');
for (const [t, want, got] of probed) console.log(`   ${t.toFixed(1).padStart(5)} s  ${want} / ${got}${want === got ? '' : '   <-- MISMATCH'}`);
console.log(`LEDs as expected    : ${ledsOk ? 'yes' : 'NO'}`);
process.exit(illegal || anomalies || !ledsOk || fires === 0 ? 1 : 0);
