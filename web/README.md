# Contourtonist — Web Demo

Try Contourtonist in the browser: **https://contourtonist-demo.stoatworks-labs.com**

This is the plugin's own DSP — the unmodified sources in [`../Source/DSP`](../Source/DSP)
— compiled to WebAssembly and run inside an AudioWorklet. The measurement-microphone
side of the product is skipped: an on-screen system fader stands in for the room, with
the simulated level derived as *reference SPL + fader dB* and fed into the real
`LoudnessController` exactly as the network level source would feed it. The rate limit,
hysteresis, gain ceiling and no-snap-on-first-measurement behaviour you see are the
plugin's, not a re-enactment. There is no acoustic feedback loop to demonstrate,
because there is no microphone hearing the result.

Pull the fader down with compensation on and the tonal balance survives the level drop;
toggle compensation off to hear what a plain volume change does. The curve pivots at
1 kHz, where its gain is identically zero by arithmetic — it can change tonal balance,
never level.

## How it works

- No JUCE shim is needed (unlike the Zero EQ simulator this borrows its structure
  from): `Source/DSP` depends on nothing but the C++ standard library, by design.
- [`wasm/contourtonist_web.cpp`](wasm/contourtonist_web.cpp) plays PluginProcessor's
  role: it owns a `LoudnessController`, refits the `ContourFilterBank` when the curve
  moves, and runs the 16-biquad bank over the audio, exposed as a C API.
- Two instances of the same module run in the page: one in the AudioWorklet (audio),
  one on the main thread (curve drawing), both fed the same fader-derived level, so
  the picture cannot drift from the sound.
- The bank runs even with compensation toggled off (the toggle selects the dry buffer)
  so its filter state stays warm and the A/B is click-free.
- A web-only "safety clip" stage (hard ceiling at −0.1 dBFS, on by default) protects
  ears and speakers; the plugin itself has no such stage.
- The demo's rate-limit default is 2 dB/s so the effect is easy to catch; the plugin
  defaults to 0.5 dB/s, because on a show nobody should hear the system EQ move.

## Build

Requires Emscripten (`brew install emscripten`) and Node.

```bash
./wasm/build.sh        # → public/contourtonist.js (single self-contained ES module)
node test/harness.mjs  # verification — must pass before deploying
```

The harness pins the wasm curve values against a native clang++ build of the same
sources (worst deviation observed: 5e-13 dB) and checks the controller's safety
properties — no snap on first measurement, rate limit arithmetic, hysteresis deadband,
ceiling-by-scaling — plus real audio through the bank against the designed response.

## Deploy

```bash
cf-run npx wrangler deploy
```
