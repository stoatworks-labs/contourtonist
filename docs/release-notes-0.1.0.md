First release.

Contourtonist measures how loud the room actually is and applies the EQ curve that keeps
the tonal balance the system was tuned for as the level moves. When a noise limit pulls
the show down 8 dB, the bass does not leave with it.

## What it does

Take the ISO 226:2003 equal-loudness contour at the reference level, take the contour at
the measured level, and the difference between them is the correction:

```
G(f) = (reference − current) − [ Lp(f, reference) − Lp(f, current) ]
```

At 1 kHz `Lp(1k, P) == P` by definition, so `G(1 kHz)` is identically zero. **The curve
can only ever change tonal balance, never level** — that is arithmetic, not a convention,
and the test suite pins it. This is a system EQ, not a slow automatic fader.

For a 100 dB reference reproduced at 85 dB: +7.9 dB at 20 Hz, +4.7 dB at 100 Hz, 0 dB at
1 kHz, −0.5 dB at 4 kHz, +2.5 dB at 12.5 kHz.

## In this release

- **VST3, AU and Standalone**, macOS universal (arm64 + x86_64).
  **macOS only this release** — the Windows guest build failed and was not diagnosed in
  time to hold the release for it. Building from source on Windows should work; the
  CMake project is not platform-specific.
- **Zero added latency.** The curve is fitted onto 16 minimum-phase biquads.
- **Level over UDP.** The standalone owns the microphone and publishes; plugin instances
  subscribe, because a plugin cannot open the measurement mic itself — the host owns the
  audio device. One mic drives every instance.
- **A deliberately trivial line protocol** (`95.3 A leq`, or just `95.3`) so any meter can
  drive it from a few lines of script, and `nc -u` is a working diagnostic.
- **Parsers for NTi XL2 responses and datalogger CSV**, including European semicolon and
  decimal-comma exports.
- Safety throughout: rate limiting with no exceptions, hysteresis, a hard gain ceiling
  that scales rather than clips the curve, a bounded tracking range so an unplugged mic
  cannot demand maximum boost, and a hold-then-release-to-flat on signal loss.

## Honest limits

- **ISO 226:2003 is validated from 20 to 90 phon**, and live sound runs above that.
  Strict conformance therefore computes nothing at show level, so the default
  extrapolates past the validated range — and says so, in the GUI and in the API.
- **The contours are for pure tones in a free field.** Treating a measured broadband
  level as a loudness level is the approximation every loudness control has made since
  Fletcher and Munson. A full ISO 532 loudness model would be more correct; it is not
  implemented.
- **The filter bank fits the curve to about 0.44 dB** worst case at the 12 dB ceiling and
  0.19 dB at 6 dB — measured across every curve the controller can request, not assumed.
- **The built-in weighting filters lose accuracy above 10 kHz** at 44.1/48 kHz. It costs
  0.31 dB on a broadband A-weighted reading of pink noise, which is irrelevant to the
  compensation and not irrelevant if you are logging that number for compliance. Feed a
  real class 1 meter in for that. See `docs/weighting.md`.
- **10EaZy is not supported.** Its protocol is not public, and inventing one for a
  noise-compliance meter is not a reasonable thing to do from inference. Use the generic
  UDP protocol, or see `docs/meters.md`.

## Verified, and not

Seven test suites pass, `auval` is clean, and the UDP transport is tested over real
sockets. The ISO 226 coefficients are checked against the standard and the filter bank's
accuracy and stability are measured across 6080 fitted sections.

**Nothing here has met hardware.** No reference sound level meter, no acoustic
calibrator, no XL2, no PA, no show. The calibration path is arithmetic that has never had
a calibrator connected to it. Review before use on live gear.

## Installing

macOS artefacts are **unsigned**, so Gatekeeper quarantines them and the DAW's plugin
scan will reject them until you clear it:

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Contourtonist.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Contourtonist.component"
xattr -dr com.apple.quarantine "/Applications/Contourtonist.app"
```

Approving the outer bundle does not unquarantine what is nested inside it.
