# Contourtonist

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The ISO 226:2003 coefficients were
> transcribed from the standard itself and are checked against it by the test suite; the
> filter bank's accuracy, the control loop's stability and the meter parsers are all
> measured rather than asserted. The AU passes `auval` and the UDP transport is tested over
> real sockets. It has **never been used on a real system, with a real microphone, at a
> real show.** Nothing here has been checked against a reference sound level meter, and no
> hardware calibrator has ever been connected. Review before use on live gear.

An equal-loudness compensation EQ that tracks the room. VST3, AU and standalone, built with
JUCE.

Contourtonist measures how loud the room actually is — from a calibrated measurement
microphone, or from an SPL meter's live output — and applies the EQ curve that keeps the
tonal balance the system was tuned for as the level moves. When a noise limit pulls the
show down 8 dB, the bass does not leave with it.

## The idea

A mix balanced at 100 dB and reproduced at 88 dB has not simply got quieter. Human hearing
is less sensitive at low frequencies at low levels, and the effect is strongly
level-dependent, so a uniform 12 dB drop takes far more perceived bass away than it takes
midrange. That relationship is exactly what ISO 226:2003's equal-loudness contours describe.

Take the contour at the reference level, take the contour at the measured level, and the
difference between them — normalised at 1 kHz — is the correction:

```
G(f) = (reference − current) − [ Lp(f, reference) − Lp(f, current) ]
```

At 1 kHz `Lp(1k, P) == P` by definition, so `G(1 kHz)` is identically zero. **The curve can
only ever change tonal balance, never level.** That is not a convention, it is arithmetic,
and the test suite pins it: this is a system EQ, not a slow automatic fader.

For a 100 dB reference reproduced at 85 dB, the correction is +7.9 dB at 20 Hz, +4.7 dB at
100 Hz, 0 dB at 1 kHz, −0.5 dB at 4 kHz and +2.5 dB at 12.5 kHz.

## Status

Version 0.1.0. Builds and passes tests; never used in anger.

| | |
|---|---|
| Formats | VST3, AU, Standalone — universal (arm64 + x86_64) |
| Tests | 7 suites, all passing (`ctest`) |
| AU validation | `auval` passes clean |
| Added latency | Zero samples, by design |
| Verified on hardware | **No** |

## How it is wired

The host owns the audio device, so a plugin instance cannot open the measurement
microphone itself. Contourtonist therefore splits in two:

- The **standalone** owns the microphone and the calibration. It measures a sliding
  short-term Leq and publishes the level over UDP.
- **Plugin instances** subscribe to that level and apply the curve to their own audio.

One microphone drives every instance in the session, which is also what a live rig with
several processing chains wants. A plugin can instead measure its own audio input, which is
right in the standalone and usually wrong in a plugin — the GUI says so.

### Level inputs

| Source | Status |
|---|---|
| Generic UDP line protocol | **Tested**, over real sockets |
| This instance's audio input | Tested against synthetic signals |
| NTi XL2 | Parser tested; **written from published docs, never seen an XL2** |
| Datalogger CSV | Parser tested, including European separators |
| 10EaZy | **Not implemented** — see `docs/meters.md` |

The generic protocol is deliberately trivial so that any meter can drive it. One reading per
UDP datagram, as text:

```
95.3 A leq
```

A bare number works too. That means a meter Contourtonist has never heard of can drive it
from a few lines of script, and that `nc -u` is a working diagnostic.

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Then run the tests:

```bash
ctest --test-dir build --output-on-failure
```

## Safety

This thing sits across a live output and changes it based on a microphone that is listening
to that same output. That is a closed loop, and the design takes it seriously:

- **The loop is negative feedback.** More boost raises the measured level, which reduces the
  demanded boost. It converges rather than running away, and `tests/test_controller.cpp`
  simulates it across a range of acoustic couplings to show that it does.
- **Rate limited.** The curve moves no faster than 0.5 dB/s by default, including on the
  very first measurement. Audible EQ movement is worse than slightly stale compensation.
- **Hard ceiling.** No point on the curve can exceed the configured maximum, and limiting
  scales the curve rather than clipping it, so its shape survives.
- **Bounded tracking range.** An unplugged microphone reads as a very quiet room; without a
  limit that becomes a demand for maximum boost.
- **Fails to flat.** If the level stops arriving, the curve holds, then releases to flat —
  the sound the system was tuned for.

## Known limits

- **ISO 226:2003 is validated from 20 to 90 phon** (80 phon above 5 kHz), and live sound
  operates above that. Strict conformance means no compensation at show level, so the
  default extrapolates beyond the validated range and says so, in the GUI and in the API.
  Set `Extend past ISO 226` off for strict behaviour.
- **The contours are for pure tones in a free field**, and programme material is neither.
  Treating a measured broadband level as a loudness level is the approximation every
  loudness control has made since Fletcher and Munson, but it is an approximation. A full
  loudness model (ISO 532) would be more correct and is not implemented.
- **The filter bank fits the curve to about 0.44 dB** worst case at the 12 dB ceiling, 0.19 dB
  at 6 dB, concentrated at 20 Hz and above 10 kHz. Measured, not assumed.
- **The built-in weighting filters lose accuracy above 10 kHz** at 44.1/48 kHz — a matched
  Z-transform aliases there. It costs 0.31 dB on a broadband A-weighted reading of pink
  noise, which is irrelevant to the compensation and not irrelevant if you are logging the
  number for compliance. Use a real meter for that, or run at 96 kHz. See `docs/weighting.md`.
- **A-weighting is a poor control input** — it *is* an equal-loudness curve, so it applies
  part of the correction twice. C is the default. Shows are still policed in dB(A), so the
  standalone measures what you need and controls on what is correct.

## Licence

MIT. See [LICENSE](LICENSE).
