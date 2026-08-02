// SPDX-License-Identifier: MIT
#include "Biquad.h"

#include <algorithm>

namespace contourtonist::dsp
{
namespace
{
/** Keep the design frequency away from Nyquist, where the bilinear transform's warping
    makes shelves and peaks meaningless and the coefficients degenerate. */
double safeFrequency (double frequencyHz, double sampleRate)
{
    return std::clamp (frequencyHz, 1.0, sampleRate * 0.49);
}
} // namespace

double Coefficients::magnitudeDb (double frequencyHz, double sampleRate) const
{
    const double w  = 2.0 * M_PI * frequencyHz / sampleRate;
    const double c1 = std::cos (w),  s1 = std::sin (w);
    const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

    const double numRe = b0 + b1 * c1 + b2 * c2;
    const double numIm =      b1 * s1 + b2 * s2;
    const double denRe = 1.0 + a1 * c1 + a2 * c2;
    const double denIm =       a1 * s1 + a2 * s2;

    const double num = numRe * numRe + numIm * numIm;
    const double den = denRe * denRe + denIm * denIm;

    if (den <= 0.0 || num <= 0.0)
        return 0.0;

    return 10.0 * std::log10 (num / den);
}

Coefficients Coefficients::peaking (double frequencyHz, double q, double gainDb,
                                    double sampleRate)
{
    const double f     = safeFrequency (frequencyHz, sampleRate);
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * M_PI * f / sampleRate;
    const double cosw0 = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.01));

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cosw0;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cosw0;
    const double a2 = 1.0 - alpha / A;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

Coefficients Coefficients::lowShelf (double frequencyHz, double gainDb, double sampleRate)
{
    const double f     = safeFrequency (frequencyHz, sampleRate);
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * M_PI * f / sampleRate;
    const double cosw0 = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);   // S = 1
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

    const double b0 =        A * ((A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha);
    const double b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    const double b2 =        A * ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha);
    const double a0 =             (A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha;
    const double a1 =     -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
    const double a2 =             (A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

Coefficients Coefficients::highShelf (double frequencyHz, double gainDb, double sampleRate)
{
    const double f     = safeFrequency (frequencyHz, sampleRate);
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * M_PI * f / sampleRate;
    const double cosw0 = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);   // S = 1
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

    const double b0 =        A * ((A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
    const double b2 =        A * ((A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha);
    const double a0 =             (A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha;
    const double a1 =      2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
    const double a2 =             (A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

} // namespace contourtonist::dsp
