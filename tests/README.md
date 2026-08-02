# Tests

Seven suites. Six of them link nothing but the standard library, so they build and run
without CMake, JUCE, an audio device, a plugin host or a microphone:

```bash
clang++ -std=c++20 -O2 -o /tmp/t tests/test_equal_loudness.cpp Source/DSP/EqualLoudness.cpp && /tmp/t
```

All of them together:

```bash
ctest --test-dir build --output-on-failure
```

| Suite | Needs JUCE | What it is really checking |
|---|---|---|
| `equal_loudness` | no | ISO 226:2003 transcription, and that the exact inverse round-trips |
| `controller` | no | That the acoustic feedback loop converges instead of ringing |
| `filter_bank` | no | How closely 16 biquads fit the curve, and that none is unstable |
| `weighting` | no | A/C/Z against IEC 61672-1's table, and what discretisation costs |
| `leq` | no | That the sliding window is a window, and calibration arithmetic |
| `meter_protocol` | no | Every way a meter line can turn into a plausible wrong number |
| `network` | **yes** | The UDP transport, over real sockets on loopback |

## They print numbers

These suites are as much measurement as verification. Every one reports the figures behind
its assertions — fit error per frequency, loop settling and overshoot per coupling,
weighting deviation per sample rate, the full 40 phon contour — because the interesting
question when changing DSP is usually "by how much did that get worse", not "did it pass".

Read the output. A suite passing with a fit error that doubled is a regression that no
assertion caught.

## Assertions are set to measured reality

Where a limit is not a physical law, the threshold is set just outside what the code
currently achieves, with a comment saying so. That makes them regression detectors rather
than aspirations. If a change makes something substantially better, tighten the assertion in
the same commit.

## What none of them can prove

Nothing here has met hardware. There is no reference sound level meter, no acoustic
calibrator, no XL2, no PA and no room in any of this. The calibration path is arithmetic
that has never had a calibrator connected to it, and the closed-loop test simulates the
acoustics with a deliberately crude model. See `docs/meters.md`.
