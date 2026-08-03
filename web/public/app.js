// Contourtonist web demo — main thread.
//
// Two instances of the same wasm module run: one inside the AudioWorklet (the audio
// path) and one here (curve drawing). Both are fed the same simulated room level
// (reference SPL + fader dB) on their own clocks, so the picture and the sound come
// from the same LoudnessController maths and cannot drift apart.

import createContourtonistModule from './contourtonist.js';

const $ = (id) => document.getElementById(id);

// --- main-thread wasm instance (curve maths) ---
const M = await createContourtonistModule();
const C = {
  init: M.cwrap('ctn_init', null, ['number']),
  setSettings: M.cwrap('ctn_set_settings', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']),
  measurement: M.cwrap('ctn_measurement', null, ['number', 'number']),
  advance: M.cwrap('ctn_advance', null, ['number']),
  status: M.cwrap('ctn_status', 'number', []),
  trackedPhon: M.cwrap('ctn_tracked_phon', 'number', []),
  extrapolated: M.cwrap('ctn_extrapolated', 'number', []),
  curveMaxAbs: M.cwrap('ctn_curve_max_abs_db', 'number', []),
  worstError: M.cwrap('ctn_fit_worst_error_db', 'number', []),
  targetDb: M.cwrap('ctn_target_db', 'number', ['number']),
  bankDb: M.cwrap('ctn_bank_db', 'number', ['number']),
};
C.init(48000); // drawing instance; the audio instance re-inits at the real device rate

// --- state ---
const HOLD_SECONDS = 10;
let faderDb = 0;
let compensate = true;
let workletPort = null;
let curveDirty = true;
let playing = false;

const settings = () => ({
  refSpl: parseFloat($('refSpl').value),
  rate: parseFloat($('rate').value),
  hyst: parseFloat($('hyst').value),
  maxGain: parseFloat($('maxGain').value),
  maxRange: 30,
  hold: HOLD_SECONDS,
  extrap: parseInt($('extrap').value, 10),
});

function pushSettings() {
  const s = settings();
  C.setSettings(s.refSpl, s.rate, s.hyst, s.maxGain, s.maxRange, s.hold, s.extrap);
  if (workletPort) workletPort.postMessage({ type: 'settings', ...s });
  curveDirty = true;
}

function pushFader() {
  if (workletPort) workletPort.postMessage({ type: 'fader', db: faderDb });
}

// Drive the drawing-side controller. An interval, not rAF: rAF throttles to zero in
// background tabs and the controller's staleness logic needs a steady clock.
let lastTracked = NaN;
setInterval(() => {
  const now = performance.now() + 1; // controller treats 0 as "no clock yet"
  C.measurement(settings().refSpl + faderDb, now);
  C.advance(now);
  const t = C.trackedPhon();
  if (t !== lastTracked) { lastTracked = t; curveDirty = true; }
  updateReadouts(); // here, not in rAF: text must stay truthful in throttled tabs
}, 100);

// --- formatting ---
const fmtHz = (v) => v >= 1000 ? `${(v / 1000).toFixed(v < 10000 ? 2 : 1)} kHz` : `${v.toFixed(0)} Hz`;
const fmtDb1 = (v) => `${v >= 0 ? '+' : ''}${v.toFixed(1)} dB`;

// ===========================================================================
// The fader
// ===========================================================================
const FADER_MIN = -24, FADER_MAX = 6;
const fader = $('fader'), faderCap = $('faderCap');

function faderY() { // cap centre, px from top of #fader
  const h = fader.clientHeight - 16; // 8 px margins match #faderTrack
  return 8 + h * (1 - (faderDb - FADER_MIN) / (FADER_MAX - FADER_MIN));
}
function renderFader() {
  faderCap.style.top = `${faderY()}px`;
  $('faderDbRead').textContent = fmtDb1(faderDb);
  $('roomLevel').innerHTML = `${(settings().refSpl + faderDb).toFixed(1)} <small>dB SPL</small>`;
}
function buildScale() {
  const wrap = $('faderScale');
  wrap.innerHTML = '';
  const h = fader.clientHeight - 16;
  for (let db = FADER_MAX; db >= FADER_MIN; db -= 3) {
    const tick = document.createElement('div');
    tick.style.top = `${8 + h * (1 - (db - FADER_MIN) / (FADER_MAX - FADER_MIN))}px`;
    const lbl = document.createElement('i');
    lbl.textContent = db === 0 ? '0' : `${db > 0 ? '+' : ''}${db}`;
    tick.appendChild(lbl);
    wrap.appendChild(tick);
  }
}
function setFader(db, fromPointer = false) {
  faderDb = Math.min(FADER_MAX, Math.max(FADER_MIN, db));
  if (!fromPointer) faderDb = Math.round(faderDb * 2) / 2;
  renderFader();
  pushFader();
  setOutputGain();
}
let faderDragging = false;
fader.addEventListener('pointerdown', (e) => {
  faderDragging = true;
  fader.setPointerCapture(e.pointerId);
  faderFromEvent(e);
});
fader.addEventListener('pointermove', (e) => { if (faderDragging) faderFromEvent(e); });
fader.addEventListener('pointerup', () => { faderDragging = false; });
fader.addEventListener('dblclick', () => setFader(0));
function faderFromEvent(e) {
  const r = fader.getBoundingClientRect();
  const h = r.height - 16;
  const p = 1 - (e.clientY - r.top - 8) / h;
  setFader(FADER_MIN + p * (FADER_MAX - FADER_MIN), true);
}
window.addEventListener('resize', () => { buildScale(); renderFader(); });

// ===========================================================================
// Curve display
// ===========================================================================
const canvas = $('curve');
const ctx2d = canvas.getContext('2d');
const FMIN = 20, FMAX = 20000, DBMAX = 14;
let cw = 0, ch = 0, dpr = 1;
let analyserPre = null, analyserPost = null;
let specBinsPre = null, specBinsPost = null;

function resizeCanvas() {
  dpr = window.devicePixelRatio || 1;
  cw = canvas.clientWidth;
  ch = canvas.clientHeight;
  canvas.width = Math.round(cw * dpr);
  canvas.height = Math.round(ch * dpr);
  curveDirty = true;
}
window.addEventListener('resize', resizeCanvas);

const xOfF = (f) => cw * Math.log(f / FMIN) / Math.log(FMAX / FMIN);
const yOfDb = (db) => ch / 2 - (db / DBMAX) * (ch / 2 - 14);

const N_POINTS = 220;
const freqGrid = new Float64Array(N_POINTS);
for (let i = 0; i < N_POINTS; i++) freqGrid[i] = FMIN * Math.pow(FMAX / FMIN, i / (N_POINTS - 1));

function drawCurve() {
  ctx2d.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx2d.clearRect(0, 0, cw, ch);

  // grid
  ctx2d.strokeStyle = '#20242b';
  ctx2d.fillStyle = '#5a606b';
  ctx2d.font = '10px sans-serif';
  ctx2d.lineWidth = 1;
  for (const f of [30, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000]) {
    const x = xOfF(f);
    ctx2d.beginPath(); ctx2d.moveTo(x, 0); ctx2d.lineTo(x, ch); ctx2d.stroke();
    ctx2d.fillText(f >= 1000 ? `${f / 1000}k` : `${f}`, x + 3, ch - 4);
  }
  for (let db = -12; db <= 12; db += 3) {
    const y = yOfDb(db);
    ctx2d.strokeStyle = db === 0 ? '#2e343d' : '#20242b';
    ctx2d.beginPath(); ctx2d.moveTo(0, y); ctx2d.lineTo(cw, y); ctx2d.stroke();
    if (db !== 0) ctx2d.fillText(`${db > 0 ? '+' : ''}${db}`, 4, y - 2);
  }

  // spectrum (pre dim fill, post line)
  if (analyserPre && playing) {
    analyserPre.getByteFrequencyData(specBinsPre);
    analyserPost.getByteFrequencyData(specBinsPost);
    const sr = audioCtx.sampleRate, nBins = specBinsPre.length;
    const binF = (i) => (i * sr) / (2 * nBins);
    const drawSpec = (bins, asFill, style) => {
      ctx2d.beginPath();
      let started = false;
      for (let i = 1; i < nBins; i++) {
        const f = binF(i);
        if (f < FMIN || f > FMAX) continue;
        const y = ch * (1 - bins[i] / 255);
        const x = xOfF(f);
        if (!started) { ctx2d.moveTo(x, asFill ? ch : y); started = true; }
        ctx2d.lineTo(x, y);
      }
      if (asFill) {
        ctx2d.lineTo(cw, ch);
        ctx2d.closePath();
        ctx2d.fillStyle = style;
        ctx2d.fill();
      } else {
        ctx2d.strokeStyle = style;
        ctx2d.lineWidth = 1;
        ctx2d.stroke();
      }
    };
    drawSpec(specBinsPre, true, 'rgba(74,90,106,0.28)');
    drawSpec(specBinsPost, false, 'rgba(127,183,217,0.8)');
  }

  const dimmed = !compensate;

  // target compensation curve (what the controller asked for)
  ctx2d.beginPath();
  for (let i = 0; i < N_POINTS; i++) {
    const y = yOfDb(C.targetDb(freqGrid[i]));
    i === 0 ? ctx2d.moveTo(xOfF(freqGrid[i]), y) : ctx2d.lineTo(xOfF(freqGrid[i]), y);
  }
  ctx2d.setLineDash([5, 4]);
  ctx2d.strokeStyle = dimmed ? 'rgba(138,143,152,0.35)' : 'rgba(138,143,152,0.8)';
  ctx2d.lineWidth = 1.3;
  ctx2d.stroke();
  ctx2d.setLineDash([]);

  // fitted bank response (what the audio actually gets)
  ctx2d.beginPath();
  for (let i = 0; i < N_POINTS; i++) {
    const y = yOfDb(C.bankDb(freqGrid[i]));
    i === 0 ? ctx2d.moveTo(xOfF(freqGrid[i]), y) : ctx2d.lineTo(xOfF(freqGrid[i]), y);
  }
  ctx2d.strokeStyle = dimmed ? 'rgba(232,184,75,0.35)' : '#e8b84b';
  ctx2d.lineWidth = 2;
  ctx2d.stroke();

  // the 1 kHz pivot: identically 0 dB by arithmetic
  ctx2d.beginPath();
  ctx2d.arc(xOfF(1000), yOfDb(0), 4, 0, Math.PI * 2);
  ctx2d.fillStyle = dimmed ? 'rgba(89,194,255,0.4)' : '#59c2ff';
  ctx2d.fill();
}

// ===========================================================================
// Readouts
// ===========================================================================
const STATUS_NAMES = ['waiting', 'tracking', 'stale', 'releasing', 'bypassed'];
function updateReadouts() {
  const s = settings();
  const st = C.status();
  const statusEl = $('statusRead');
  statusEl.textContent = STATUS_NAMES[st] ?? '?';
  statusEl.className = st === 1 ? 'tracking' : 'waiting';
  $('roomRead').textContent = `${(s.refSpl + faderDb).toFixed(1)} dB SPL`;
  $('trackedRead').textContent = `${C.trackedPhon().toFixed(1)} phon`;
  $('peakRead').textContent = `${C.curveMaxAbs().toFixed(2)} dB`;
  $('fitRead').textContent = `${C.worstError().toFixed(2)} dB`;
  // A flat curve rests on no extrapolation regardless of level — only warn when the
  // curve actually contains something.
  $('extrapBadge').style.display =
    C.extrapolated() && s.extrap === 1 && C.curveMaxAbs() > 0.05 ? 'inline-block' : 'none';
}

// ===========================================================================
// Audio graph + sources
// ===========================================================================
let audioCtx = null, workletNode = null, srcGain = null, faderGain = null;
let activeSources = [];
let uploadedBuffer = null;
const bufferCache = new Map();

function makeNoiseBuffer(kind, seconds = 4) {
  const len = Math.round(seconds * audioCtx.sampleRate);
  const buf = audioCtx.createBuffer(2, len, audioCtx.sampleRate);
  for (let chn = 0; chn < 2; chn++) {
    const d = buf.getChannelData(chn);
    let b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (let i = 0; i < len; i++) {
      const white = Math.random() * 2 - 1;
      if (kind === 'white') {
        d[i] = white * 0.5;
      } else {
        // Paul Kellet's economy pink noise filter
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        d[i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11;
        b6 = white * 0.115926;
      }
    }
  }
  return buf;
}

// Synthesized 4-bar demo groove at 110 BPM (all in-browser, nothing licensed).
// Same groove as the Zero EQ simulator — kick and bass give the equal-loudness
// compensation something real to work on.
async function makeGrooveBuffer() {
  const sr = audioCtx.sampleRate;
  const bpm = 110, beat = 60 / bpm, bars = 4, dur = bars * 4 * beat;
  const off = new OfflineAudioContext(2, Math.round(dur * sr), sr);
  const master = off.createGain();
  master.gain.value = 0.55;
  const comp = off.createDynamicsCompressor();
  comp.threshold.value = -14; comp.ratio.value = 3; comp.attack.value = 0.005; comp.release.value = 0.12;
  master.connect(comp).connect(off.destination);

  const noiseBuf = off.createBuffer(1, Math.round(sr * 0.5), sr);
  const nd = noiseBuf.getChannelData(0);
  for (let i = 0; i < nd.length; i++) nd[i] = Math.random() * 2 - 1;

  const env = (g, t, a, peak, d) => {
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(peak, t + a);
    g.gain.exponentialRampToValueAtTime(0.001, t + a + d);
  };
  const kick = (t) => {
    const o = off.createOscillator(), g = off.createGain();
    o.frequency.setValueAtTime(130, t);
    o.frequency.exponentialRampToValueAtTime(44, t + 0.11);
    env(g, t, 0.002, 0.9, 0.26);
    o.connect(g).connect(master);
    o.start(t); o.stop(t + 0.3);
  };
  const snare = (t) => {
    const s = off.createBufferSource(); s.buffer = noiseBuf;
    const bp = off.createBiquadFilter(); bp.type = 'bandpass'; bp.frequency.value = 1900; bp.Q.value = 0.6;
    const g = off.createGain();
    env(g, t, 0.001, 0.5, 0.16);
    s.connect(bp).connect(g).connect(master);
    s.start(t); s.stop(t + 0.2);
    const o = off.createOscillator(); o.frequency.value = 190;
    const g2 = off.createGain();
    env(g2, t, 0.001, 0.35, 0.09);
    o.connect(g2).connect(master);
    o.start(t); o.stop(t + 0.12);
  };
  const hat = (t, open) => {
    const s = off.createBufferSource(); s.buffer = noiseBuf;
    const hp = off.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 7200;
    const g = off.createGain();
    env(g, t, 0.001, open ? 0.16 : 0.13, open ? 0.25 : 0.035);
    s.connect(hp).connect(g).connect(master);
    s.start(t); s.stop(t + (open ? 0.3 : 0.06));
  };
  const bassNote = (t, freq, len) => {
    const o = off.createOscillator(); o.type = 'triangle'; o.frequency.value = freq;
    const lp = off.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 900; lp.Q.value = 0.9;
    const g = off.createGain();
    env(g, t, 0.004, 0.5, len);
    o.connect(lp).connect(g).connect(master);
    o.start(t); o.stop(t + len + 0.05);
  };
  const chord = (t, freqs, len) => {
    for (const f of freqs) {
      for (const det of [-5, 5]) {
        const o = off.createOscillator(); o.type = 'sawtooth';
        o.frequency.value = f; o.detune.value = det;
        const lp = off.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 1400;
        const g = off.createGain();
        g.gain.setValueAtTime(0, t);
        g.gain.linearRampToValueAtTime(0.045, t + 0.08);
        g.gain.setValueAtTime(0.045, t + len - 0.25);
        g.gain.linearRampToValueAtTime(0, t + len);
        o.connect(lp).connect(g).connect(master);
        o.start(t); o.stop(t + len);
      }
    }
  };

  const A1 = 55, C2 = 65.41, D2 = 73.42, E2 = 82.41, G2 = 98, F1 = 43.65;
  const bassPatterns = [
    [A1, 0, A1, A1, 0, C2, 0, E2],
    [A1, 0, A1, A1, 0, G2, 0, D2],
    [F1, 0, F1, F1, 0, C2, 0, A1],
    [F1, 0, F1, F1, 0, E2, 0, G2],
  ];
  const chords = [
    [220, 261.63, 329.63],        // Am
    [220, 261.63, 329.63],
    [174.61, 220, 261.63, 349.23], // F
    [174.61, 220, 261.63, 329.63],
  ];
  for (let bar = 0; bar < bars; bar++) {
    const t0 = bar * 4 * beat;
    chord(t0, chords[bar], 4 * beat);
    for (let b = 0; b < 4; b++) kick(t0 + b * beat);
    snare(t0 + 1 * beat); snare(t0 + 3 * beat);
    for (let e = 0; e < 8; e++) hat(t0 + e * beat / 2, bar === bars - 1 && e === 7);
    bassPatterns[bar].forEach((f, e) => { if (f) bassNote(t0 + e * beat / 2, f, beat * 0.45); });
  }
  return off.startRendering();
}

async function getBuffer(kind) {
  if (kind === 'file') return uploadedBuffer;
  if (bufferCache.has(kind)) return bufferCache.get(kind);
  let buf;
  if (kind === 'white' || kind === 'pink') buf = makeNoiseBuffer(kind);
  else if (kind === 'groove') buf = await makeGrooveBuffer();
  bufferCache.set(kind, buf);
  return buf;
}

async function ensureAudio() {
  if (audioCtx) return;
  audioCtx = new AudioContext();
  await audioCtx.audioWorklet.addModule('worklet.js');
  workletNode = new AudioWorkletNode(audioCtx, 'contourtonist', {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [2],
  });
  workletNode.port.onmessage = (e) => {
    const msg = e.data;
    if (msg.type === 'ready') {
      workletPort = workletNode.port;
      pushSettings();
      pushFader();
      workletPort.postMessage({ type: 'compensate', on: compensate });
      workletPort.postMessage({ type: 'safetyClip', on: $('safetyClip').checked });
    } else if (msg.type === 'status') {
      onStatus(msg);
    }
  };
  srcGain = audioCtx.createGain();
  faderGain = audioCtx.createGain();
  srcGain.connect(faderGain);
  faderGain.connect(workletNode);
  analyserPre = audioCtx.createAnalyser();
  analyserPost = audioCtx.createAnalyser();
  analyserPre.fftSize = 4096; analyserPost.fftSize = 4096;
  analyserPre.smoothingTimeConstant = 0.82; analyserPost.smoothingTimeConstant = 0.82;
  specBinsPre = new Uint8Array(analyserPre.frequencyBinCount);
  specBinsPost = new Uint8Array(analyserPost.frequencyBinCount);
  faderGain.connect(analyserPre);
  workletNode.connect(analyserPost);
  workletNode.connect(audioCtx.destination);
  setTrim();
  setOutputGain();
}

function setTrim() {
  if (srcGain) srcGain.gain.value = Math.pow(10, parseFloat($('srcLevel').value) / 20);
}
function setOutputGain() {
  // The fader IS the playback gain: pulling it down genuinely gets quieter, and the
  // simulated room level falls with it. Short ramp so rides don't zipper.
  if (faderGain && audioCtx)
    faderGain.gain.setTargetAtTime(Math.pow(10, faderDb / 20), audioCtx.currentTime, 0.02);
}

function stopSources() {
  for (const s of activeSources) { try { s.stop(); } catch { /* already stopped */ } }
  activeSources = [];
}

async function startSources() {
  const kind = $('sourceSel').value;
  stopSources();
  if (kind === 'sine') {
    const o = audioCtx.createOscillator();
    o.frequency.value = sineFreqValue();
    o.connect(srcGain);
    o.start();
    activeSources.push(o);
  } else {
    const buf = await getBuffer(kind);
    if (!buf) { alert('Choose an audio file first.'); return false; }
    const s = audioCtx.createBufferSource();
    s.buffer = buf; s.loop = true;
    s.connect(srcGain);
    s.start();
    activeSources.push(s);
  }
  return true;
}

$('playBtn').addEventListener('click', async () => {
  if (!playing) {
    await ensureAudio();
    await audioCtx.resume();
    if (!(await startSources())) return;
    playing = true;
    $('playBtn').textContent = '■ Stop';
    $('playBtn').classList.add('playing');
  } else {
    stopSources();
    if (workletPort) workletPort.postMessage({ type: 'reset' });
    playing = false;
    curveDirty = true;
    $('playBtn').textContent = '▶ Play';
    $('playBtn').classList.remove('playing');
    drawMeter($('inMeter'), 0);
    drawMeter($('outMeter'), 0);
  }
});

$('sourceSel').addEventListener('change', async () => {
  $('sineFreqRow').style.display = $('sourceSel').value === 'sine' ? '' : 'none';
  if ($('sourceSel').value === 'file' && !uploadedBuffer) $('fileInput').click();
  if (playing) await startSources();
});
$('fileInput').addEventListener('change', async () => {
  const f = $('fileInput').files[0];
  if (!f) return;
  await ensureAudio();
  uploadedBuffer = await audioCtx.decodeAudioData(await f.arrayBuffer());
  $('sourceSel').value = 'file';
  if (playing) await startSources();
});
$('sourceSel').addEventListener('dblclick', () => { if ($('sourceSel').value === 'file') $('fileInput').click(); });

const sineFreqValue = () => 20 * Math.pow(1000, parseFloat($('sineFreq').value));
$('sineFreq').addEventListener('input', () => {
  $('sineFreqVal').textContent = fmtHz(sineFreqValue());
  for (const s of activeSources) if (s.frequency) s.frequency.value = sineFreqValue();
});
$('sineFreqVal').textContent = fmtHz(sineFreqValue());
$('srcLevel').addEventListener('input', () => {
  $('srcLevelVal').textContent = `${parseFloat($('srcLevel').value).toFixed(1)} dB`;
  setTrim();
});
$('safetyClip').addEventListener('change', () => {
  if (workletPort) workletPort.postMessage({ type: 'safetyClip', on: $('safetyClip').checked });
});

// --- settings bindings ---
function bindSetting(id, valId, fmt) {
  $(id).addEventListener('input', () => {
    if (valId) $(valId).textContent = fmt(parseFloat($(id).value));
    pushSettings();
    renderFader();
  });
}
bindSetting('refSpl', 'refSplVal', (v) => `${v.toFixed(0)} dB`);
bindSetting('rate', 'rateVal', (v) => `${v.toFixed(1)} dB/s`);
bindSetting('hyst', 'hystVal', (v) => `${v.toFixed(1)} dB`);
bindSetting('maxGain', 'maxGainVal', (v) => `${v.toFixed(1)} dB`);
$('extrap').addEventListener('change', pushSettings);

$('compBtn').addEventListener('click', () => {
  compensate = !compensate;
  $('compBtn').textContent = compensate ? 'Compensation ON' : 'Compensation OFF';
  $('compBtn').classList.toggle('on', compensate);
  if (workletPort) workletPort.postMessage({ type: 'compensate', on: compensate });
  curveDirty = true;
});

// ===========================================================================
// Meters + render loop
// ===========================================================================
let meterIn = 0, meterOut = 0;
function onStatus(s) {
  meterIn = Math.max(meterIn * 0.85, s.inPeak);
  meterOut = Math.max(meterOut * 0.85, s.outPeak);
}
function drawMeter(canvasEl, peak) {
  const c = canvasEl.getContext('2d');
  const w = canvasEl.width, h = canvasEl.height;
  c.clearRect(0, 0, w, h);
  const db = 20 * Math.log10(peak + 1e-6);
  const y = h * (1 - Math.min(1, Math.max(0, (db + 60) / 60)));
  const grad = c.createLinearGradient(0, h, 0, 0);
  grad.addColorStop(0, '#3f9d55');
  grad.addColorStop(0.8, '#c9c93f');
  grad.addColorStop(1, '#d95f3f');
  c.fillStyle = grad;
  c.fillRect(3, y, w - 6, h - y);
}

function frame() {
  if (playing || curveDirty) {
    drawCurve();
    curveDirty = false;
  }
  if (playing) {
    drawMeter($('inMeter'), meterIn);
    drawMeter($('outMeter'), meterOut);
  }
  requestAnimationFrame(frame);
}

resizeCanvas();
buildScale();
renderFader();
updateReadouts();
frame();
