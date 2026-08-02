# AGENTS.md — bringing an LLM up to speed on Contourtonist

Orientation for an AI assistant (or a new human) picking this project up cold. Read this
before proposing changes. `CLAUDE.md` holds the short command reference; this file explains
the *why*, and the traps.

---

## 1. What this is

An **equal-loudness compensation EQ for live sound**, built with JUCE in C++, shipping as
VST3, AU and Standalone from one target. It measures how loud the room is, looks up the
ISO 226:2003 contours at that level and at a reference level, and applies the difference as
a zero-latency minimum-phase EQ. As the show level moves, the curve moves with it.

Public repo, MIT, ships a user-facing AI-assisted disclaimer in the README. Version 0.1.0.
Never used on a real system.

## 2. The rules that matter most

**The curve is level-neutral.** `G(1 kHz)` is identically zero, by arithmetic, not by
convention. If a change makes the plugin apply broadband gain it has become a slow automatic
fader, and it has also put gain into a feedback loop that currently has none. There is a
test for this; it is not decoration.

**Zero added latency.** No lookahead, no linear phase, no FFT convolution, no oversampling
in the audio path. This lives across a live output where latency means lip sync errors and
comb filtering against nearby sources.

**The rate limit has no exceptions.** Including on the first measurement — see the comment
in `LoudnessController::measurement`, which explains why the obvious "snap to the first
reading" optimisation is wrong. The rate limit is the single property that makes this safe
to put across a PA.

**Uncertainty is surfaced, never smoothed over.** Extrapolation beyond ISO 226's range,
missing calibration, unparsed datagrams, the filter bank's own fit error — all of it reaches
the GUI. A system EQ that silently does something plausible is worse than one that admits
it is unsure.

## 3. Layout

```
Source/
  DSP/                       Depends on nothing but the standard library. Keep it that way.
    EqualLoudness.{h,cpp}      ISO 226:2003 contours and the compensation curve
    LoudnessController.{h,cpp} Level in, curve out: rate limit, hysteresis, ceiling, hold
    ContourFilterBank.{h,cpp}  Least-squares fit of the curve onto 16 biquads
    Biquad.{h,cpp}             RBJ cookbook designs + Direct Form I section
    Weighting.{h,cpp}          A/C/Z per IEC 61672-1
    ShortTermLeq.{h,cpp}       Sliding Leq integrator with calibration
  Level/
    MeterProtocol.{h,cpp}      All meter text parsing, as pure functions
    NetworkLevel.{h,cpp}       UDP publish/subscribe (the only part needing JUCE)
  GUI/CurveDisplay.{h,cpp}     The curve plot
  PluginProcessor.{h,cpp}      Host entry point, threading, state
  PluginEditor.{h,cpp}         The window
tests/                         Seven suites; six need no JUCE at all
```

**The `DSP/` and `Level/MeterProtocol` split is load-bearing.** Those files compile against
the standard library alone, which is why the test suite runs without a plugin host, an audio
device or a microphone. If something in there starts needing `juce_core`, it belongs
somewhere else.

## 4. The traps

### 4.1 ISO 226:2003's own equations are not exact inverses

Equation (1) gives SPL from phons; equation (2) gives phons from SPL. They disagree by up to
0.056 dB, because the standard rounds constants independently in each — most visibly `0.4`
standing in for `10^-0.4 = 0.398107`, worth 0.0206 dB on its own.

This is the standard's, not ours. `loudnessLevel()` therefore defaults to an *algebraic*
inverse of equation (1), which round-trips to machine precision, and offers equation (2) as
`InverseMethod::standardEquation2`. Do not "fix" the exact inverse to match the printed one.
There is a test that documents the discrepancy so this cannot be quietly undone.

### 4.2 The validated range excludes the entire use case

ISO 226:2003 applies from 20 phon to 90 phon, and only to 80 phon above 5 kHz. A live system
tuned to 100 dB and pulled to 92 dB has *both* endpoints above the ceiling, so a strictly
conformant implementation computes nothing at all at exactly the levels this product exists
for.

Hence `ExtrapolationPolicy`. `extend` is the default and evaluates the equation past the
validated range — smooth and finite well past 100 phon, just unsupported by listening data.
Every result carries `Contour::extrapolated` and the GUI shows it. **Do not remove that flag
to tidy the API.**

Related: the per-frequency ceiling (90 below 5 kHz, 80 above) is deliberately *not* applied
per frequency. Clamping HF to 80 while LF reaches 90 would put a step in the contour between
4 and 5 kHz, which is a step in the EQ — an artefact invented by the implementation. A
single global limit keeps the curve smooth; `isWithinValidRange()` still reports the true
per-frequency limit so the flag stays honest.

### 4.3 The acoustic loop is negative feedback, and that is the good news

The microphone hears the PA, including whatever the plugin just did. More LF boost raises
the measured level, which shrinks (reference − current), which reduces the boost. It
converges. `tests/test_controller.cpp` simulates the loop across couplings from 0 to 0.8 and
shows overshoot under 0.004 dB and no oscillation.

Two consequences that *are* real: the loop settles slightly short of the requested curve
(`LoopCompensation::estimated` divides it out if you want), and a loop with delay and enough
gain would ring, which is why the rate limit sits far below the measurement bandwidth.

### 4.4 A-weighting is the wrong control input

A-weighting *is* an equal-loudness curve — an approximation to the inverse 40 phon contour.
Feed an A-weighted level in as a loudness level and part of the correction gets applied
twice, at a curve fixed to 40 phon regardless of actual level. C is the default. Shows are
policed in dB(A) so the standalone measures it for display, but control runs on C.

### 4.5 Bilinear transform ruins weighting filters

A bilinear transform sends s = ∞ to z = −1, planting a zero at Nyquist that the analogue
definition does not have. A-weighting has six poles and four zeros, so it gets a double zero
at Nyquist and reads **11.6 dB low at 20 kHz at 48 kHz** — while being exactly right at
1 kHz, which is where anyone spot-checking would look. Pre-warping the poles does not help;
the forced zero is a property of the map.

`Weighting.cpp` uses a matched Z-transform instead, which does not create the extra zeros.
It aliases instead: 4.5 dB at 20 kHz, 1.1 dB at 10 kHz, at 48 kHz. Better, not fixed. A
2-parameter numerical refit of the 12.2 kHz section was tried and reaches 1.03 dB at 48 kHz
and 0.033 dB at 96 kHz — not enough of a win to justify abandoning a principled design, but
the notes are here if someone wants to take it further with a higher-order fit or by
oversampling the measurement path.

### 4.6 CMake: architectures must be set before `project()`

`CMAKE_OSX_ARCHITECTURES` set after `project()` is accepted silently, appears in the cache,
and produces an arm64-only binary. The build log gives no hint. **Verify with `lipo -archs`,
never the log.** This bit this project once already.

### 4.7 `std::from_chars` for floats needs macOS 26

The deployment target is 11.0, so `MeterProtocol.cpp` parses through an `istringstream`
imbued with `std::locale::classic()`. That is not a stylistic choice: `strtod` honours
`LC_NUMERIC`, so on a machine set to a decimal-comma locale it reads `95.3` as `95` —
silently, on someone else's computer and not on yours.

## 5. Threading

Three threads, and the split is the thing to get right:

- **Audio thread** — `processBlock` only. Reads coefficients, writes audio. No allocation,
  no locks, no fitting.
- **Timer thread** (30 Hz) — reads the level source, advances the controller, refits the
  bank, publishes coefficients. All the expensive work lives here.
- **Receiver thread** — takes datagrams off the socket.

Coefficients cross to the audio thread through a double buffer and an atomic index: the
timer writes the buffer the index does not point at, then flips. Worst case the audio thread
is one update (33 ms) stale, which against a 0.5 dB/s rate limit is 0.017 dB.

## 6. What is verified, and what is not

Measured and tested: the ISO 226 coefficients against the standard, the exact inverse to
machine precision, filter fit accuracy and stability across 6080 fitted sections, control
loop convergence under simulated acoustic coupling, weighting against IEC 61672-1's table,
Leq behaviour including a 10-minute drift check, every meter parser, and the UDP transport
over real sockets. `auval` passes.

**Not verified:** anything involving real hardware. No reference meter, no calibrator, no
XL2, no 10EaZy, no PA, no show. The calibration path in particular is arithmetic that has
never met a calibrator. See `docs/meters.md`.

## 7. Deliberately not done

- **ISO 532 loudness model.** More correct than pure-tone contours for broadband material,
  substantially more DSP, hard to verify without reference data. The contour-difference
  engine sits behind a narrow enough interface to add it later.
- **10EaZy support.** Its protocol is not publicly documented and guessing at a protocol for
  a noise-compliance meter is not a good idea. Needs access to real hardware or documentation.
- **Oversampled measurement path.** Would fix §4.5 properly.
- **Sidechain input.** The network-companion design was chosen instead; a sidechain bus would
  be a reasonable second route into the same controller.
