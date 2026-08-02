# Frequency weighting — accuracy and its limits

The short version: **A and C weighting are essentially exact below 8 kHz at every sample
rate, and lose accuracy above 10 kHz at 44.1 and 48 kHz.** For driving the compensation
curve this does not matter. For reading a number into a noise log it does, and the fix is to
feed a real meter in rather than to squint at this filter.

## Why it is hard

IEC 61672-1 defines the weightings as analogue transfer functions: poles at 20.599 Hz
(twice), 107.653 Hz, 737.862 Hz and 12194.2 Hz (twice) for A, with four zeros at DC.

The obvious way to turn that into biquads is a bilinear transform, and it is wrong here in a
way that is easy to miss. **The bilinear map sends s = ∞ to z = −1**, so it plants a zero at
Nyquist in any transfer function with more poles than zeros. A-weighting has six poles and
four zeros, so it acquires a double zero at Nyquist that the analogue definition does not
have, and the response is dragged down hard as it approaches it.

Measured, at 48 kHz:

| | bilinear error |
|---|---|
| 1 kHz | 0.00 dB |
| 16 kHz | 3.9 dB |
| 20 kHz | **11.6 dB** |

Exactly right at 1 kHz, which is where anyone spot-checking it would look, and double
figures wrong at the top of the band. Pre-warping the poles does not help — the forced zero
is a property of the map, not of where the poles sit.

## What is used instead

A **matched Z-transform**: each pole and zero mapped individually, a pole at s = −ω becoming
a pole at z = e^(−ωT) and a zero at s = 0 becoming a zero at z = 1. The order difference
shows up as zeros at the origin, which are pure delay and do not touch the magnitude
response. No forced zero at Nyquist.

It aliases instead — the digital response is the analogue one plus its images folded back —
so it is better rather than fixed:

| sample rate | ≤ 8 kHz | 10 kHz | > 12.5 kHz |
|---|---|---|---|
| 44.1 kHz | < 0.1 dB | 1.27 dB | 3.25 dB |
| 48 kHz | < 0.1 dB | 1.10 dB | 4.50 dB |
| 96 kHz | < 0.1 dB | 0.30 dB | 1.21 dB |
| 192 kHz | < 0.1 dB | 0.08 dB | 0.31 dB |

C-weighting matches A to within a thousandth of a dB throughout, because the error lives
entirely in the 12.2 kHz pole pair the two share.

## What it costs

On **pink noise** — which has as much energy in the top third-octave as in any other, and so
is harsher than any real programme material — the broadband A-weighted reading at 48 kHz
comes out **0.31 dB high**.

For the compensation curve that is nothing: 0.31 dB of level error moves the curve about
0.16 dB at 20 Hz, a fifth of the filter bank's own fitting error and a small fraction of
what moving the microphone a metre would do.

For a compliance figure it is not nothing, and there is a better answer than making this
filter marginally less wrong: **use a class 1 meter and feed it in over one of the meter
inputs.** That is what a compliance context should be doing anyway, and it is why those
inputs exist. Failing that, run the standalone at 96 kHz or above.

## What was tried

A 2-parameter numerical refit of the 12.2 kHz section — optimising the pole frequency and a
zero position against the analogue reference over 20 Hz to 20 kHz — reaches:

| sample rate | matched Z | refitted |
|---|---|---|
| 44.1 kHz | 5.07 dB | 1.25 dB |
| 48 kHz | 4.37 dB | 1.03 dB |
| 96 kHz | 1.17 dB | **0.033 dB** |

Excellent at 96 kHz, and still over a dB at 48 kHz, because two free parameters cannot beat
aliasing that close to Nyquist. Not enough of a win at the rates that matter to justify
replacing a principled transform with a fitted one, so it was not taken.

**If someone wants to finish this properly**, the two routes are a higher-order fit designed
directly in the digital domain — the filter bank in `ContourFilterBank.cpp` already contains
a working least-squares fitter that could be pointed at this — or oversampling the
measurement path, which fixes it outright at the cost of a resampler.

## Testing

`tests/test_weighting.cpp` checks two independent things against each other: the analogue
transfer function, and the standard's own printed table transcribed by hand. They agree to
0.05 dB, which is the table's printing precision — a formula and a table arrived at
separately agreeing that closely means both are right.

The table is evaluated at **exact midband frequencies** (1000 × 10^(n/10)), not the rounded
nominal labels. IEC states its tolerances against the exact values, and using 12500 Hz
instead of 12589.25 Hz is worth a couple of tenths of a dB at the ends of the band — enough
to fail a test that is actually correct.
