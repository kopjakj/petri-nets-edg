/* Run study.bin on rp2040js 1.4.2 with one configuration and check the trace.
 *
 * usage: node run.js <study.bin> <bootrom.cjs> <net> <policy> <seed> [jitter=1] [target=2000] [qdepth=16]
 * env:   MAX_S (simulated limit, default 30), STALL_S (no firing, default 2),
 *        TRACE_OUT=<file>
 * Prints one JSON line.
 *
 * Checks, by replaying "F t" lines in an independent reference token game built
 * from the T/M0 lines the firmware prints (and cross-checked against nets.json
 * when present):
 *   illegal   firing with an empty input place
 *   anomalies "!!" lines (protocol anomaly, choice error, route error)
 *   end       target | dead (no transition enabled) | stall (one enabled) |
 *             qfull (a grouped send timed out) | limit (simulated time)
 *   counts    firings per transition
 *   starve    longest run of consecutive firings of other transitions while
 *             a transition stayed enabled in the reference marking
 *   markings  distinct reference markings visited
 */
const fs = require('fs');
const path = require('path');
const { Simulator } = require('rp2040js');

const [, , binPath, romPath, net, pol, seed, jit = '1', target = '2000', qd = '16'] = process.argv;
const MAX_S = Number(process.env.MAX_S || 30);
const STALL_S = Number(process.env.STALL_S || 2);
const WALL_MS = Number(process.env.WALL_MS || 600000);

const bin = Buffer.from(fs.readFileSync(binPath));
let off = -1;
for (let i = 0; i + 32 <= bin.length; i += 4)
  if (bin.readUInt32LE(i) === 0x50E7C0F1 && bin.readUInt32LE(i + 28) === 0x50E7C0F2) { off = i; break; }
if (off < 0) { console.error('CFG block not found'); process.exit(2); }
[net, pol, seed, jit, target, qd].forEach((v, k) => bin.writeUInt32LE(Number(v) >>> 0, off + 4 + 4 * k));

const { bootromB1 } = require(path.resolve(romPath));
const sim = new Simulator();
const clock = sim.clock;
const mcu = sim.rp2040;
mcu.logger = { debug() {}, info() {}, warn() {}, error() {} };
mcu.loadBootrom(bootromB1);
mcu.flash.set(bin, 0);
mcu.core.PC = 0x10000000;

let buf = '', lastFire = 0, started = 0, finished = false, nonet = false;
const lines = [];
mcu.uart[0].onByte = (b) => {
  if (b !== 10) { buf += String.fromCharCode(b); return; }
  const s = buf.replace(/\r$/, ''); buf = '';
  const t = clock.nanos / 1e9;
  lines.push({ t, s });
  if (s.startsWith('F ')) lastFire = t;
  else if (s === 'START') { started = t; lastFire = t; }
  else if (s.startsWith('END') || s.startsWith('QFULL') || s.startsWith('DEAD')) finished = true;
  else if (s === 'NONET') nonet = true;
};

const wall0 = Date.now();
const cycleNanos = 1e9 / 125000000;
let why = 'limit';
for (let n = 0; ; ++n) {
  if (mcu.core.waiting) {
    let d = clock.nanosToNextAlarm;
    if (!(d > 0) || !isFinite(d)) d = 1e6;
    clock.tick(d);
    if (mcu.core.interruptsUpdated && mcu.core.checkForInterrupts()) mcu.core.waiting = false;
  } else {
    clock.tick(mcu.core.executeInstruction() * cycleNanos);
  }
  if ((n & 0xfff) !== 0) continue;
  const now = clock.nanos / 1e9;
  if (finished || nonet) { why = 'finished'; break; }
  if (started && now - lastFire > STALL_S) { why = 'quiet'; break; }
  if (now > MAX_S) { why = 'limit'; break; }
  if (Date.now() - wall0 > WALL_MS) { why = 'wall'; break; }
}
const simS = clock.nanos / 1e9;
if (process.env.TRACE_OUT) fs.writeFileSync(process.env.TRACE_OUT, lines.map((l) => l.t.toFixed(4) + '\t' + l.s).join('\n') + '\n');

/* --- reference replay ---------------------------------------------------- */
const out = { net: Number(net), pol: Number(pol), seed: Number(seed), jit: Number(jit) };
if (nonet) { out.end = 'nonet'; console.log(JSON.stringify(out)); process.exit(0); }
const T = []; let M = null, hdr = null, heap = null, groups = null, keep = null, qmax = null;
let fires = 0, illegal = 0, anomalies = 0, malformed = 0, qfull = 0, deadCentral = false, endSeen = false;
const seq = [];
for (const { s } of lines) {
  let m;
  if ((m = s.match(/^NET (\S+) np=(\d+) nt=(\d+) POL (\S+)/))) { hdr = { name: m[1], np: +m[2], nt: +m[3], pol: m[4] }; continue; }
  if ((m = s.match(/^T (\d+) (\S+) pre((?: \d+)*) post((?: \d+)*)$/))) {
    T[+m[1]] = { name: m[2], pre: m[3].trim().split(/\s+/).filter(Boolean).map(Number), post: m[4].trim().split(/\s+/).filter(Boolean).map(Number) };
    continue;
  }
  if ((m = s.match(/^M0((?: \d+)+)$/))) { M = m[1].trim().split(/\s+/).map(Number); continue; }
  if ((m = s.match(/^HEAP free=(\d+)$/))) { heap = +m[1]; continue; }
  if ((m = s.match(/^GROUPS (\d+)$/))) { groups = +m[1]; continue; }
  if ((m = s.match(/^REL keep-every-offer=(\d+)$/))) { keep = +m[1]; continue; }
  if ((m = s.match(/^END fired=(\d+) qmax=(\d+)$/))) { endSeen = true; qmax = +m[2]; continue; }
  if (s.startsWith('QFULL')) { qfull++; continue; }
  if (s === 'DEAD central') { deadCentral = true; continue; }
  if (s.includes('!!')) { anomalies++; continue; }
  if ((m = s.match(/^F (\d+)$/))) { seq.push(+m[1]); continue; }
  if (s === 'START' || s === '') continue;
  malformed++;
}
const nets = fs.existsSync(path.join(__dirname, 'nets.json')) ? JSON.parse(fs.readFileSync(path.join(__dirname, 'nets.json'))) : null;
let mismatch = false;
if (nets && hdr) {
  const ref = nets.find((x) => x.name === hdr.name);
  mismatch = !ref || JSON.stringify(ref.m0) !== JSON.stringify(M) ||
    JSON.stringify(ref.T.map((t) => [t.pre, t.post])) !== JSON.stringify(T.map((t) => [t.pre, t.post]));
}
const enabled = (t) => T[t].pre.every((p) => M[p] > 0);
const key = () => M.join(',');
const visited = new Set([key()]);
const counts = new Array(T.length).fill(0);
const streak = new Array(T.length).fill(0), maxStreak = new Array(T.length).fill(0);
/* strong fairness: firings since t last fired, in windows where t was enabled at least once */
const gap = new Array(T.length).fill(0), seen = new Array(T.length).fill(false), maxGap = new Array(T.length).fill(0);
for (let u = 0; u < T.length; ++u) seen[u] = enabled(u);
for (const t of seq) {
  if (!T[t]) { malformed++; continue; }
  fires++;
  for (let u = 0; u < T.length; ++u) {
    if (u !== t && enabled(u)) { if (++streak[u] > maxStreak[u]) maxStreak[u] = streak[u]; }
    else streak[u] = 0;
  }
  if (!enabled(t)) illegal++;
  T[t].pre.forEach((p) => M[p]--);
  T[t].post.forEach((p) => M[p]++);
  counts[t]++;
  visited.add(key());
  for (let u = 0; u < T.length; ++u) {
    if (u === t) { gap[u] = 0; seen[u] = enabled(u); continue; }
    gap[u]++;
    if (enabled(u)) seen[u] = true;
    if (seen[u] && gap[u] > maxGap[u]) maxGap[u] = gap[u];
  }
}
const anyEnabled = T.some((_, t) => enabled(t));
let end;
if (endSeen) end = 'target';
else if (qfull) end = 'qfull';
else if (why === 'quiet' || deadCentral) end = anyEnabled ? 'stall' : 'dead';
else end = why;
let worst = 0;
maxStreak.forEach((v, t) => { if (v > maxStreak[worst]) worst = t; });
Object.assign(out, {
  name: hdr && hdr.name, polName: hdr && hdr.pol, end, fires, illegal, anomalies, malformed, mismatch,
  qfull, qmax, heap, groups, keep, simS: +simS.toFixed(3), wallS: +((Date.now() - wall0) / 1000).toFixed(1),
  markings: [...visited], counts, maxStreak, starve: maxStreak[worst] || 0, starveT: T[worst] && T[worst].name,
  maxGap, gapMax: Math.max(0, ...maxGap), gapT: T[maxGap.indexOf(Math.max(...maxGap))] && T[maxGap.indexOf(Math.max(...maxGap))].name,
  final: M,
});
console.log(JSON.stringify(out));
