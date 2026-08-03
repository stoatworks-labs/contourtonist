// AudioWorklet host for the Contourtonist wasm core. All DSP happens inside the wasm
// module (the plugin's own Source/DSP code); this file only shuttles samples and
// messages. The demo has no measurement mic: the page sends the fader position, and
// this worklet derives the simulated room level (reference + fader) and feeds it to
// the real LoudnessController on the audio clock — rate limit, hysteresis and ceiling
// are the plugin's own, not a re-enactment.

import createContourtonistModule from './contourtonist.js';

const ADVANCE_INTERVAL_BLOCKS = 4;  // ~11 ms at 48 kHz
const STATUS_INTERVAL_BLOCKS = 6;   // ~16 ms

class ContourtonistProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ready = false;
    this.compensate = true;   // A/B: false outputs the dry signal (bank keeps running
                              // so its state stays warm and the toggle is click-free)
    this.safetyClip = true;
    this.faderDb = 0;
    this.refSpl = 100;
    this.pendingSettings = null;

    createContourtonistModule().then((M) => {
      this.M = M;
      this.C = {
        init: M.cwrap('ctn_init', null, ['number']),
        setSettings: M.cwrap('ctn_set_settings', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']),
        measurement: M.cwrap('ctn_measurement', null, ['number', 'number']),
        advance: M.cwrap('ctn_advance', null, ['number']),
        bufL: M.cwrap('ctn_buf_l', 'number', []),
        bufR: M.cwrap('ctn_buf_r', 'number', []),
        process: M.cwrap('ctn_process', null, ['number']),
        resetAudio: M.cwrap('ctn_reset_audio', null, []),
        status: M.cwrap('ctn_status', 'number', []),
        trackedPhon: M.cwrap('ctn_tracked_phon', 'number', []),
      };
      this.C.init(sampleRate);
      if (this.pendingSettings) this.applySettings(this.pendingSettings);
      this.pL = this.C.bufL() >> 2;
      this.pR = this.C.bufR() >> 2;
      this.blockCount = 0;
      this.advanceCount = 0;
      this.inPeak = 0;
      this.outPeak = 0;
      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    });

    this.port.onmessage = (e) => {
      const msg = e.data;
      if (msg.type === 'settings') {
        if (this.ready) this.applySettings(msg); else this.pendingSettings = msg;
      } else if (msg.type === 'fader') {
        this.faderDb = msg.db;
      } else if (msg.type === 'compensate') {
        this.compensate = !!msg.on;
      } else if (msg.type === 'safetyClip') {
        this.safetyClip = !!msg.on;
      } else if (msg.type === 'reset') {
        if (this.ready) this.C.resetAudio();
      }
    };
  }

  applySettings(s) {
    this.refSpl = s.refSpl;
    this.C.setSettings(s.refSpl, s.rate, s.hyst, s.maxGain, s.maxRange, s.hold, s.extrap);
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.ready || out.length === 0) return true;

    const input = inputs[0];
    const n = out[0].length; // render quantum (128)
    const H = this.M.HEAPF32;
    const inL = input.length > 0 ? input[0] : null;
    const inR = input.length > 1 ? input[1] : inL;

    // Feed the controller on the audio clock. The measurement is refreshed every
    // tick, matching the plugin's continuously-arriving network level source.
    if (++this.advanceCount >= ADVANCE_INTERVAL_BLOCKS) {
      this.advanceCount = 0;
      const tMs = currentTime * 1000;
      this.C.measurement(this.refSpl + this.faderDb, tMs);
      this.C.advance(tMs);
    }

    for (let i = 0; i < n; i++) {
      H[this.pL + i] = inL ? inL[i] : 0;
      H[this.pR + i] = inR ? inR[i] : 0;
    }

    this.C.process(n); // always: keeps filter state warm across the A/B toggle

    const ceil = 0.988553; // -0.1 dBFS, web-only safety stage
    const oL = out[0];
    const oR = out.length > 1 ? out[1] : null;
    let ip = this.inPeak, op = this.outPeak;
    for (let i = 0; i < n; i++) {
      const dl = inL ? inL[i] : 0;
      const dr = inR ? inR[i] : dl;
      let l = this.compensate ? H[this.pL + i] : dl;
      let r = this.compensate ? H[this.pR + i] : dr;
      if (this.safetyClip) {
        l = Math.max(-ceil, Math.min(ceil, l));
        r = Math.max(-ceil, Math.min(ceil, r));
      }
      oL[i] = l;
      if (oR) oR[i] = r;
      const ai = Math.max(Math.abs(dl), Math.abs(dr));
      const ao = Math.max(Math.abs(l), Math.abs(r));
      if (ai > ip) ip = ai;
      if (ao > op) op = ao;
    }
    this.inPeak = ip;
    this.outPeak = op;

    if (++this.blockCount >= STATUS_INTERVAL_BLOCKS) {
      this.blockCount = 0;
      this.port.postMessage({
        type: 'status',
        inPeak: this.inPeak,
        outPeak: this.outPeak,
        tracked: this.C.trackedPhon(),
        status: this.C.status(),
      });
      this.inPeak = 0;
      this.outPeak = 0;
    }

    return true;
  }
}

registerProcessor('contourtonist', ContourtonistProcessor);
