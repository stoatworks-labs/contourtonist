# Notes

Working notes for this repo: status, decisions, and the traps that have actually bitten.
Migrated out of Claude Code's memory on 2026-08-24, so they are written in the first
person and dated by when each thing was learned — that date is usually the useful part.

Cross-cutting notes that are not specific to this repo live in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

*Contourtonist — equal-loudness (ISO 226) compensation EQ tracking short-term LEQ. JUCE VST3/AU/Standalone. PUBLIC MIT, RELEASED v0.1.0 macOS-only; used in REAPER, never in a live environment*

**Contourtonist** — equal-loudness compensation EQ for **live sound**. Started 2026-08-02.
`~/Projects/contourtonist`, **PUBLIC MIT** at https://github.com/stoatworks-labs/contourtonist.
**Released v0.1.0 on 2026-08-02 — macOS only** (dmg + multi-part pkg; the Parallels
Windows guest build failed and was not held for). On the website, and video
**`KHg28Tmo9F8`** is live. **Instagram reel NOT done** — `make_social.py contourtonist`
ran clean but produced no 9:16 and no caption entry; that is the one outstanding item.

**Web demo LIVE** (2026-08-03) at **https://contourtonist-demo.stoatworks-labs.com**
(Workers custom domain; attaching it DISABLED the workers.dev route, which now 404s) —
`web/` in the repo, patterned on [zero eq](https://github.com/stoatworks-labs/zero-eq/blob/main/docs/NOTES.md) (`zero-eq`)'s sim but with **no JUCE shim**
(Source/DSP is stdlib-only). No mic: an on-screen system fader stands in for the room
(level = reference SPL + fader dB) feeding the real LoudnessController. Harness pins wasm
against a native clang++ build (5e-13 dB). Deploy from `web/`: `cf-run npx wrangler deploy`.
Launch entry `contourtonist-demo` in ~/.claude/launch.json. Website: `"demo"` key in
projects.json (site-wide pattern, renders the "Try the live demo" button) + an overview
paragraph, deployed 2026-08-03.

JUCE 8.0.6, one target → **VST3 + AU + Standalone**, universal arm64+x86_64, like
[zero eq](https://github.com/stoatworks-labs/zero-eq/blob/main/docs/NOTES.md) (`zero-eq`) whose CMakeLists was the template.

## What it does

Measures room level, looks up the ISO 226:2003 contour at that level and at a reference
level, applies the difference as EQ. As the show level moves the curve moves.

    G(f) = (ref − cur) − [ Lp(f,ref) − Lp(f,cur) ]

`G(1 kHz)` is **identically zero by arithmetic**, so it can only change tonal balance,
never level. That property is load-bearing and tested — break it and the thing becomes a
slow automatic fader *and* puts gain into the acoustic loop.

## Architecture decisions the user made 2026-08-02

Live sound (not studio/cinema); **network companion** for the mic path (standalone owns
the mic + calibration and publishes over UDP, plugin instances subscribe — a plugin
can't open the measurement mic because the host owns the device); all four meter inputs
wanted (generic OSC/UDP, NTi XL2, 10EaZy, CSV tail); **ISO 226 contour difference**, not
a full ISO 532 loudness model.

## Three findings worth keeping

**ISO 226:2003's own equations (1) and (2) are not exact inverses** — they disagree by up
to 0.056 dB because the standard rounds constants independently in each (biggest: `0.4`
standing in for `10^-0.4 = 0.398107`, worth 0.0206 dB). The *algebraic* inverse of eq (1)
round-trips to machine precision, so that is the default; eq (2) is kept as
`InverseMethod::standardEquation2`. Don't "fix" this.

**The validated range excludes the entire use case.** ISO 226 applies 20–90 phon (80
above 5 kHz). A system tuned at 100 dB pulled to 92 has *both* endpoints above the
ceiling, so strict conformance computes nothing at exactly show level. Hence
`ExtrapolationPolicy::extend` as default, with `Contour::extrapolated` surfaced to the
GUI everywhere.

**The acoustic loop is NEGATIVE feedback, not positive.** More boost → higher measured
level → smaller (ref−cur) → less boost. It converges. Simulated across couplings 0–0.8:
overshoot <0.004 dB, no oscillation, settled boost *falls* as coupling rises. The real
risks are a slightly offset equilibrium and ringing if rate-limited too fast — not
runaway. See [bilinear weighting trap](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_bilinear_weighting_trap.md) for the other big gotcha found here.

## Measured, not claimed

Filter bank fits the curve to **0.44 dB** worst case at the 12 dB ceiling (0.19 dB at
6 dB) using **16** minimum-phase biquads — 12 sections gave 0.6 dB. Zero added latency.
6080 fitted sections checked, none unstable. `auval` clean. UDP transport tested over
real sockets. 7 test suites; **6 of them link nothing but libstdc++** so the DSP tests
run with a bare `clang++` — keep `Source/DSP/` and `MeterProtocol` JUCE-free.

## Never verified

**Loaded and used inside REAPER** (Allan, 2026-08-02), but **not in a live
environment**, and the acoustic end has met no hardware at all: no reference meter, no acoustic calibrator, no XL2, no
10EaZy, no PA, no show. The calibration path is arithmetic that has never met a
calibrator. **10EaZy is deliberately NOT implemented** — its protocol isn't public and
guessing at a noise-compliance meter's protocol is a bad idea; XL2 has a tested parser
but the transport was written from docs and never run against a meter.

Screenshot verification failed twice here (wrong display, then no Quartz) — matches
**screenshot capture** (working-practice note, kept in Claude memory). A JUCE-linked socket integration test was written
instead and is better evidence anyway.


## The standalone hang, fixed 2026-08-02

JUCE's stock standalone wrapper opens an audio **input** for any plugin with input
channels. On this Mac — which has NDI, L-ISA, Pro Tools Bridge and Teams virtual devices
— that builds an `AudioIODeviceCombiner` across two devices and **blocks forever in
`AudioDeviceCreateIOProcID`, on the message thread, before the window is created**. The
app launches, sits in the Dock, uses no CPU, logs nothing, produces no crash report and
never shows a window. **Zero EQ's standalone hangs identically**, so test with that
before assuming a new bug.

`Source/StandaloneApp.cpp` fixes it with `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` and a
first-run seeded device setup (output, no input). Three routes that do **not** work and
are documented in that file: `preferredSetupOptions` is overwritten by
`insertDefaultDeviceNames`; a `{0,2}` channel constraint also zeroes the settings dialog
so an input can never be re-enabled; `StandalonePluginHolder::init` is not virtual. Only
saved device XML is honoured verbatim (`initialiseFromXML` never calls
`insertDefaultDeviceNames`).

## Release harness trap

`git describe --tags` exits 128 with no tags and `set -o pipefail` carries it out of the
pipeline, so `scripts/release-local.sh` died silently before its first echo. Needs
`|| true`. Only bites on a **first** release, which is why zero-eq never hit it.
