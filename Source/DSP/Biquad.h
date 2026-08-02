// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>

/**
    A second-order IIR section, and the RBJ cookbook designs for the three shapes the
    filter bank needs.

    Standard-library only, so the whole filter bank can be fitted and measured in a test
    without an audio device.

    Direct Form I is used deliberately. Transposed Direct Form II is the usual choice
    and has better numerical behaviour at low frequencies, but it holds state that is a
    function of the *coefficients*, so changing coefficients on a running filter causes
    a transient proportional to how much they changed. This bank retunes continuously —
    that is the entire point of the product — so a topology whose state is just past
    inputs and outputs, and therefore stays meaningful when the coefficients move, is
    worth more here than the noise-floor advantage. See docs/filter-bank.md.
*/
namespace contourtonist::dsp
{

struct Coefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;   // normalised so a0 == 1

    /** Magnitude response at @p frequencyHz, in dB. */
    double magnitudeDb (double frequencyHz, double sampleRate) const;

    /** A section that does nothing. */
    static Coefficients identity() { return {}; }

    /** RBJ peaking EQ. */
    static Coefficients peaking (double frequencyHz, double q, double gainDb,
                                 double sampleRate);

    /** RBJ low shelf, S = 1. */
    static Coefficients lowShelf (double frequencyHz, double gainDb, double sampleRate);

    /** RBJ high shelf, S = 1. */
    static Coefficients highShelf (double frequencyHz, double gainDb, double sampleRate);
};

/** One Direct Form I biquad, single channel. */
class Biquad
{
public:
    void setCoefficients (const Coefficients& c) noexcept { coeffs = c; }
    const Coefficients& getCoefficients() const noexcept { return coeffs; }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }

    double processSample (double x) noexcept
    {
        const double y = coeffs.b0 * x + coeffs.b1 * x1 + coeffs.b2 * x2
                       - coeffs.a1 * y1 - coeffs.a2 * y2;

        x2 = x1; x1 = x;
        y2 = y1; y1 = y;

        return y;
    }

private:
    Coefficients coeffs;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
};

} // namespace contourtonist::dsp
