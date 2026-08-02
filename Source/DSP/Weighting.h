// SPDX-License-Identifier: MIT
#pragma once

#include "Biquad.h"

#include <array>
#include <cstddef>

/**
    A, C and Z frequency weighting, per IEC 61672-1.

    Used by the standalone's own microphone measurement path. When level arrives from an
    external meter it is already weighted by that meter and this is not involved.

    ## Which weighting drives the compensation

    Not A, if you can avoid it. A-weighting *is* an equal-loudness curve — it
    approximates the inverse of the 40 phon contour — so feeding an A-weighted level in
    as a loudness level applies part of the equal-loudness correction twice, and does so
    with a curve fixed at 40 phon regardless of the actual level. C or Z weighted is the
    right input for estimating a loudness level.

    Shows are still policed in dB(A), so the standalone measures all three and sends
    whichever is configured for control while displaying the one the operator needs.

    ## Accuracy, and the trap

    The weightings are defined as analogue transfer functions, and turning those into
    biquads is where the accuracy goes. A bilinear transform plants a zero at Nyquist
    that the analogue definition does not have, which drags the top of the band down by
    11.6 dB at 20 kHz at a 48 kHz sample rate — while remaining exactly right at 1 kHz,
    where everyone checks it. This implementation uses a matched Z-transform instead;
    see the comment on `matchedZ` in the .cpp for why that helps and what it costs.

    **Measured accuracy** against the analogue definition, worst case (A-weighting; C is
    within a thousandth of a dB of the same, because the error is entirely in the
    12.2 kHz pole pair the two share):

        sample rate    <= 8 kHz    10 kHz    > 12.5 kHz
          44.1 kHz      < 0.1 dB   1.27 dB     3.25 dB
            48 kHz      < 0.1 dB   1.10 dB     4.50 dB
            96 kHz      < 0.1 dB   0.30 dB     1.21 dB
           192 kHz      < 0.1 dB   0.08 dB     0.31 dB

    On pink noise — a harsher case than any real programme, having as much energy in the
    top third-octave as in any other — the broadband A-weighted reading at 48 kHz comes
    out 0.31 dB high.

    ## What that means in practice

    For **driving the compensation**, nothing. A 0.31 dB error in the measured level
    moves the curve about 0.16 dB at 20 Hz, which is a fifth of the filter bank's own
    fitting error and a small fraction of what moving the microphone a metre would do.

    For **reading a number off the screen and putting it in a noise log**, it matters,
    and the fix is not to squint at this filter: it is to feed Contourtonist from a real
    class 1 meter over one of the four supported meter inputs. That is what a compliance
    context should be doing anyway, and it is why those inputs exist. Failing that,
    run the standalone at 96 kHz or above.

    Oversampling the measurement path would fix it properly and is not implemented.
    See docs/weighting.md.

    tests/test_weighting.cpp measures all of the above against the tabulated values of
    IEC 61672-1 at exact midband frequencies (10^(n/10) x 1 kHz, not the rounded nominal
    ones — the standard's tolerances are stated against the exact values).
*/
namespace contourtonist::dsp
{

enum class Weighting
{
    z,   ///< No weighting. Flat.
    a,   ///< A-weighting.
    c    ///< C-weighting.
};

/** Longest weighting chain: A-weighting is sixth order, so three sections. */
inline constexpr std::size_t maxWeightingSections = 3;

/** A weighting filter for one channel. */
class WeightingFilter
{
public:
    /** Design for @p weighting at @p sampleRate. Clears state. */
    void prepare (Weighting weighting, double sampleRate);

    void reset() noexcept;

    double processSample (double x) noexcept
    {
        for (std::size_t i = 0; i < activeSections; ++i)
            x = sections[i].processSample (x);

        return x;
    }

    /** Combined magnitude response at @p frequencyHz, in dB. Zero at 1 kHz for A and C
        by construction, and zero everywhere for Z. */
    double magnitudeDb (double frequencyHz) const;

    Weighting getWeighting() const noexcept { return kind; }

private:
    std::array<Biquad, maxWeightingSections> sections {};
    std::size_t activeSections = 0;
    Weighting kind = Weighting::z;
    double rate = 48000.0;
};

/** The IEC 61672-1 tabulated weighting for @p frequencyHz, in dB — the value the filter
    is trying to hit. Interpolated in log-frequency between tabulated points. Used by the
    tests, and available for plotting a reference curve in the GUI. */
double referenceWeightingDb (Weighting weighting, double frequencyHz);

} // namespace contourtonist::dsp
