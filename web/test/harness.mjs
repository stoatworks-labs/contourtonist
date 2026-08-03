// Verification harness for the Contourtonist wasm core. Node-only; not shipped.
//
// Same discipline as the plugin's own test suites: real numbers out of the actual
// compiled module, checked against independently derived expectations. The curve
// values are pinned against a NATIVE clang++ build of the same sources
// (scratch ctn_ref.cpp: compensationCurve at fixed ref/cur pairs), so this catches
// wasm-vs-native drift, not just self-consistency. Audio goes through the bank in
// 128-sample chunks, matching the AudioWorklet render quantum, with a two-tone
// stimulus (content inside AND outside the boosted region) so a broadband gain error
// and a correct contour EQ produce different outputs.
//
// Run: node web/test/harness.mjs

import createContourtonistModule from '../public/contourtonist.js';

const SR = 48000;
const BLOCK = 128;

const M = await createContourtonistModule();
const C = {
  init: M.cwrap('ctn_init', null, ['number']),
  setSettings: M.cwrap('ctn_set_settings', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']),
  resetController: M.cwrap('ctn_reset_controller', null, []),
  measurement: M.cwrap('ctn_measurement', null, ['number', 'number']),
  advance: M.cwrap('ctn_advance', null, ['number']),
  bufL: M.cwrap('ctn_buf_l', 'number', []),
  bufR: M.cwrap('ctn_buf_r', 'number', []),
  process: M.cwrap('ctn_process', null, ['number']),
  resetAudio: M.cwrap('ctn_reset_audio', null, []),
  status: M.cwrap('ctn_status', 'number', []),
  trackedPhon: M.cwrap('ctn_tracked_phon', 'number', []),
  extrapolated: M.cwrap('ctn_extrapolated', 'number', []),
  curveMaxAbs: M.cwrap('ctn_curve_max_abs_db', 'number', []),
  worstError: M.cwrap('ctn_fit_worst_error_db', 'number', []),
  targetDb: M.cwrap('ctn_target_db', 'number', ['number']),
  bankDb: M.cwrap('ctn_bank_db', 'number', ['number']),
  directDb: M.cwrap('ctn_curve_direct_db', 'number', ['number', 'number', 'number']),
};

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!cond) failures++;
}

// Default settings, one place. Rate is passed per-test because the rate limit is the
// property several tests are about.
function setup({ ref = 100, rate = 0.5, hyst = 1.0, maxGain = 12, maxRange = 30, hold = 10, extrap = 1 } = {}) {
  C.init(SR);
  C.setSettings(ref, rate, hyst, maxGain, maxRange, hold, extrap);
  C.resetController();
}

// Feed a constant level and advance the controller on a simulated clock until the
// tracked value stops moving (or timeout). Returns simulated seconds elapsed.
function settle(level, { stepMs = 100, timeoutS = 120 } = {}) {
  let t = 1000;
  let last = NaN;
  for (let s = 0; s < (timeoutS * 1000) / stepMs; s++) {
    C.measurement(level, t);
    C.advance(t);
    const now = C.trackedPhon();
    if (now === last && s > 2) return (t - 1000) / 1000;
    last = now;
    t += stepMs;
  }
  return (t - 1000) / 1000;
}

function sine(freq, seconds, amp = 0.25) {
  const buf = new Float32Array(Math.round(seconds * SR));
  for (let i = 0; i < buf.length; i++) buf[i] = amp * Math.sin(2 * Math.PI * freq * i / SR);
  return buf;
}
function addInPlace(a, b) { for (let i = 0; i < a.length; i++) a[i] += b[i]; return a; }

// Goertzel magnitude (dBFS of the sine component) over [from, to).
function toneDb(buf, freq, from, to) {
  const w = 2 * Math.PI * freq / SR;
  const coeff = 2 * Math.cos(w);
  let s0 = 0, s1 = 0, s2 = 0;
  for (let i = from; i < to; i++) {
    s0 = buf[i] + coeff * s1 - s2;
    s2 = s1; s1 = s0;
  }
  const power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  const amp = 2 * Math.sqrt(Math.max(power, 0)) / (to - from);
  return 20 * Math.log10(amp + 1e-15);
}

function processAudio(input) {
  const out = new Float32Array(input.length);
  const pL = C.bufL() >> 2, pR = C.bufR() >> 2;
  for (let start = 0; start < input.length; start += BLOCK) {
    const n = Math.min(BLOCK, input.length - start);
    for (let i = 0; i < n; i++) {
      M.HEAPF32[pL + i] = input[start + i];
      M.HEAPF32[pR + i] = input[start + i];
    }
    C.process(n);
    for (let i = 0; i < n; i++) out[start + i] = M.HEAPF32[pL + i];
  }
  return out;
}

console.log(`\n=== Contourtonist wasm core verification (SR=${SR}, block=${BLOCK}) ===\n`);

// --- 1. Curve values match the native build to machine precision ---
// Reference values printed by a native clang++ -O2 build of the same
// EqualLoudness.cpp (compensationCurve().gainAt()), 2026-08-03.
{
  const native = [
    [100, 90, [[20, 5.291691918475], [40, 4.486364359066], [100, 3.165410685684],
               [400, 0.929386985452], [1000, 0.0], [4000, -0.339802564011],
               [10000, 0.760111881874], [12500, 1.675766663630]]],
    [100, 76, [[20, 12.687047176674], [40, 10.739703163501], [100, 7.564424782590],
               [400, 2.212391302299], [1000, 0.0], [4000, -0.828745376016],
               [10000, 1.803031254094], [12500, 3.995126344782]]],
    [100, 106, [[20, -3.177053913581], [40, -2.696127878055], [100, -1.904336793183],
                [400, -0.560476021323], [1000, 0.0], [4000, 0.201807267491],
                [10000, -0.459395930759], [12500, -1.009643144993]]],
  ];
  setup();
  let worst = 0;
  for (const [ref, cur, points] of native)
    for (const [f, g] of points)
      worst = Math.max(worst, Math.abs(C.directDb(ref, cur, f) - g));
  check('curve matches native clang++ build', worst < 1e-9, `worst |Δ| = ${worst.toExponential(2)} dB`);
}

// --- 2. Level neutrality: G(1 kHz) identically zero ---
{
  setup();
  let worst = 0;
  for (const cur of [70, 76, 85, 90, 95, 100, 103, 106])
    worst = Math.max(worst, Math.abs(C.directDb(100, cur, 1000)));
  check('G(1 kHz) identically zero across levels', worst < 1e-12, `worst ${worst.toExponential(2)} dB`);
}

// --- 3. Sign convention: quieter restores bass, louder cuts it ---
{
  setup();
  check('quieter than reference boosts LF', C.directDb(100, 90, 40) > 1);
  check('louder than reference cuts LF', C.directDb(100, 106, 40) < -1);
}

// --- 4. First measurement does not snap (the rate-limit guarantee) ---
{
  setup({ rate: 0.5 });
  C.measurement(85, 1000); // 15 dB down, out of nowhere
  C.advance(1000);
  C.advance(1100); // 100 ms later
  const moved = Math.abs(100 - C.trackedPhon());
  check('first measurement: curve slews from flat, no snap', moved < 0.2,
        `tracked moved ${moved.toFixed(3)} phon after 100 ms`);
}

// --- 5. Rate limit honoured during the slew ---
{
  setup({ rate: 0.5, hyst: 0 });
  C.measurement(88, 1000);
  C.advance(1000);
  // 4 simulated seconds at 0.5 dB/s curve rate = 0.5/0.53 phon/s
  let t = 1000;
  for (let i = 0; i < 40; i++) { t += 100; C.measurement(88, t); C.advance(t); }
  const expected = 100 - (0.5 / 0.53) * 4;
  check('rate limit: tracked phon after 4 s matches rate', Math.abs(C.trackedPhon() - expected) < 0.05,
        `tracked ${C.trackedPhon().toFixed(3)}, expected ${expected.toFixed(3)}`);
}

// --- 6. Hysteresis: small wiggles do not move the curve ---
{
  setup({ rate: 50, hyst: 1.0 });
  settle(92);
  const before = C.trackedPhon();
  let t = 1e6;
  for (let i = 0; i < 50; i++) { t += 100; C.measurement(92 + (i % 2 ? 0.4 : -0.4), t); C.advance(t); }
  check('hysteresis: ±0.4 dB wiggle leaves curve untouched', C.trackedPhon() === before,
        `tracked ${C.trackedPhon().toFixed(3)}`);
}

// --- 7. Ceiling: curve scaled to max gain, never beyond ---
{
  setup({ rate: 50, maxGain: 12, maxRange: 30 });
  settle(70); // 30 dB down -> unclamped curve peaks ~15 dB
  check('max gain ceiling holds', C.curveMaxAbs() <= 12 + 1e-9, `peak ${C.curveMaxAbs().toFixed(3)} dB`);
  // Scaled, not clipped: 1 kHz still exactly zero, shape preserved.
  check('ceiling scales, 1 kHz still zero', Math.abs(C.targetDb(1000)) < 1e-12);
}

// --- 8. Extrapolation flag surfaced at show levels ---
{
  setup({ rate: 50 });
  settle(90);
  check('extrapolated flag set above validated range', C.extrapolated() === 1);
}

// --- 9. Filter bank fit: within the plugin's measured accuracy ---
{
  setup({ rate: 50, maxGain: 12 });
  settle(76); // 24 dB down, curve at the 12 dB ceiling — the documented worst case
  check('fit worst error reported below 0.6 dB at 12 dB ceiling', C.worstError() < 0.6,
        `${C.worstError().toFixed(3)} dB`);
  let worst = 0;
  for (let i = 0; i <= 200; i++) {
    const f = 20 * Math.pow(1000, i / 200); // 20 Hz .. 20 kHz
    if (f > 16000) continue;
    worst = Math.max(worst, Math.abs(C.bankDb(f) - C.targetDb(f)));
  }
  check('bank response tracks target over 20 Hz–16 kHz', worst < 0.6, `worst ${worst.toFixed(3)} dB`);
}

// --- 10. Audio through the bank matches the designed response (two-tone) ---
{
  setup({ rate: 50 });
  settle(90);
  C.resetAudio();
  const input = addInPlace(sine(40, 1.5, 0.15), sine(1000, 1.5, 0.15));
  const out = processAudio(input);
  const settleN = SR / 2, end = Math.floor(1.5 * SR);
  const g40 = toneDb(out, 40, settleN, end) - toneDb(input, 40, settleN, end);
  const g1k = toneDb(out, 1000, settleN, end) - toneDb(input, 1000, settleN, end);
  const d40 = C.bankDb(40), d1k = C.bankDb(1000);
  check('audio: 40 Hz gain matches designed bank response', Math.abs(g40 - d40) < 0.3,
        `measured ${g40.toFixed(2)}, designed ${d40.toFixed(2)}`);
  check('audio: 40 Hz is boosted (bass restored)', g40 > 3, `${g40.toFixed(2)} dB`);
  check('audio: 1 kHz within fit error of unity', Math.abs(g1k) < C.worstError() + 0.1,
        `${g1k.toFixed(3)} dB, fit error ${C.worstError().toFixed(3)} dB`);
  check('audio: 1 kHz measured matches designed', Math.abs(g1k - d1k) < 0.3,
        `measured ${g1k.toFixed(3)}, designed ${d1k.toFixed(3)}`);
}

// --- 11. Flat at reference: bank is audibly a wire ---
{
  setup({ rate: 50 });
  settle(100);
  C.resetAudio();
  const input = addInPlace(sine(40, 1.0, 0.15), sine(8000, 1.0, 0.15));
  const out = processAudio(input);
  const g40 = toneDb(out, 40, SR / 2, SR) - toneDb(input, 40, SR / 2, SR);
  const g8k = toneDb(out, 8000, SR / 2, SR) - toneDb(input, 8000, SR / 2, SR);
  check('at reference: 40 Hz unity', Math.abs(g40) < 0.05, `${g40.toFixed(3)} dB`);
  check('at reference: 8 kHz unity', Math.abs(g8k) < 0.05, `${g8k.toFixed(3)} dB`);

  // Zero added latency. Checked at the flat curve, where the bank should be a wire:
  // an impulse must come out at sample 0, full size. (With the curve engaged a
  // cross-correlation test would be wrong, not the DSP — a minimum-phase LF boost has
  // legitimate low-frequency group delay, which is phase shift, not added latency.)
  C.resetAudio();
  const impulse = new Float32Array(BLOCK);
  impulse[0] = 1;
  const impOut = processAudio(impulse);
  let tail = 0;
  for (let i = 1; i < BLOCK; i++) tail = Math.max(tail, Math.abs(impOut[i]));
  check('zero added latency: impulse emerges at sample 0', Math.abs(impOut[0] - 1) < 1e-3,
        `out[0] = ${impOut[0].toFixed(6)}`);
  check('zero added latency: no delayed energy', tail < 1e-3, `max tail ${tail.toExponential(2)}`);
}

// --- 12. Output stays finite through a full fader ride ---
{
  setup({ rate: 6 });
  let t = 1000;
  let allFinite = true;
  const pL = C.bufL() >> 2;
  let phase = 0;
  for (let s = 0; s < 3000; s++) { // ~8 s of audio while the level rides -24..+6
    const level = 100 + 6 - 30 * Math.abs(Math.sin(s / 300));
    t += BLOCK / SR * 1000;
    C.measurement(level, t);
    C.advance(t);
    for (let i = 0; i < BLOCK; i++) {
      M.HEAPF32[pL + i] = 0.25 * Math.sin(phase);
      phase += 2 * Math.PI * 55 / SR;
    }
    C.process(BLOCK);
    for (let i = 0; i < BLOCK; i++)
      if (!Number.isFinite(M.HEAPF32[pL + i])) { allFinite = false; break; }
  }
  check('fader ride: output finite through continuous refits', allFinite);
}

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`}\n`);
process.exit(failures === 0 ? 0 : 1);
