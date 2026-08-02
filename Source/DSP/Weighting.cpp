// SPDX-License-Identifier: MIT
#include "Weighting.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace contourtonist::dsp
{
namespace
{

// IEC 61672-1 pole frequencies, in Hz.
constexpr double f1 =    20.598997;
constexpr double f2 =   107.65265;
constexpr double f3 =   737.86223;
constexpr double f4 = 12194.217;

/** Analogue A-weighting response at @p f, unnormalised. */
std::complex<double> analogueA (double f)
{
    const std::complex<double> s { 0.0, 2.0 * M_PI * f };
    const double w1 = 2.0 * M_PI * f1, w2 = 2.0 * M_PI * f2;
    const double w3 = 2.0 * M_PI * f3, w4 = 2.0 * M_PI * f4;

    const std::complex<double> num = (w4 * w4) * s * s * s * s;
    const std::complex<double> den = (s + w1) * (s + w1) * (s + w2) * (s + w3)
                                   * (s + w4) * (s + w4);
    return num / den;
}

/** Analogue C-weighting response at @p f, unnormalised. */
std::complex<double> analogueC (double f)
{
    const std::complex<double> s { 0.0, 2.0 * M_PI * f };
    const double w1 = 2.0 * M_PI * f1, w4 = 2.0 * M_PI * f4;

    const std::complex<double> num = (w4 * w4) * s * s;
    const std::complex<double> den = (s + w1) * (s + w1) * (s + w4) * (s + w4);
    return num / den;
}

/** A biquad realising two real analogue poles and @p zerosAtDc zeros at s = 0, by the
    matched Z-transform.

    ## Why not the bilinear transform

    The obvious approach is a bilinear transform, and it is wrong here in a way that is
    easy to miss. The bilinear map sends s = infinity to z = -1, so it plants a zero at
    Nyquist in any transfer function with more poles than zeros. A-weighting has six
    poles and four zeros, so it gets a double zero at Nyquist that the analogue
    definition does not have, and the response is dragged down hard as it approaches it.
    At 48 kHz that is about 4 dB of error at 16 kHz and 11.6 dB at 20 kHz — while
    remaining exactly right at 1 kHz, which is where anyone spot-checking would look.
    Pre-warping the poles does not help; the forced zero is a property of the map, not
    of where the poles are.

    The matched Z-transform maps each pole and zero individually — a pole at s = -w
    becomes a pole at z = exp(-wT), a zero at s = 0 becomes a zero at z = 1 — and simply
    does not create the extra zeros. The remaining order difference shows up as zeros at
    the origin, which are a pure delay and do not touch the magnitude response.

    It is not perfect either: the matched Z-transform aliases, so the response near
    Nyquist is the analogue response plus its images. That error is a fraction of a dB
    where the bilinear one was double figures. tests/test_weighting.cpp measures it. */
Coefficients matchedZ (double poleAHz, double poleBHz, int zerosAtDc, double sampleRate)
{
    const double pa = std::exp (-2.0 * M_PI * poleAHz / sampleRate);
    const double pb = std::exp (-2.0 * M_PI * poleBHz / sampleRate);

    // Denominator (1 - pa z^-1)(1 - pb z^-1), so a1 = -(pa + pb), a2 = pa*pb.
    const double a1 = -(pa + pb);
    const double a2 = pa * pb;

    // Numerator: each zero at s = 0 becomes a factor (1 - z^-1). Any shortfall in
    // order is left as zeros at the origin, which contribute only delay.
    double n0 = 1.0, n1 = 0.0, n2 = 0.0;

    switch (zerosAtDc)
    {
        case 2:   n0 = 1.0; n1 = -2.0; n2 = 1.0; break;   // (1 - z^-1)^2
        case 1:   n0 = 1.0; n1 = -1.0; n2 =  0.0; break;   // (1 - z^-1)
        case 0:
        default:  n0 = 1.0; n1 =  0.0; n2 =  0.0; break;   // flat numerator
    }

    return { n0, n1, n2, a1, a2 };
}

} // namespace

double referenceWeightingDb (Weighting weighting, double frequencyHz)
{
    if (weighting == Weighting::z)
        return 0.0;

    // Computed from the analogue definition rather than a transcribed table. The
    // definition is what IEC 61672-1 normatively specifies; its printed table is a
    // rounded presentation of exactly this, so computing it avoids introducing
    // transcription errors and gives full precision at every frequency rather than at
    // 34 of them. tests/test_weighting.cpp checks a sample of it against the printed
    // table to confirm the two agree.
    const auto response = weighting == Weighting::a ? analogueA (frequencyHz)
                                                    : analogueC (frequencyHz);
    const auto atKilohertz = weighting == Weighting::a ? analogueA (1000.0)
                                                       : analogueC (1000.0);

    return 20.0 * std::log10 (std::abs (response) / std::abs (atKilohertz));
}

void WeightingFilter::prepare (Weighting weighting, double sampleRate)
{
    kind = weighting;
    rate = sampleRate;
    reset();

    switch (weighting)
    {
        case Weighting::a:
            // Six poles: f1 (twice), f2, f3, f4 (twice). Four zeros at DC.
            sections[0].setCoefficients (matchedZ (f1, f1, 2, sampleRate));
            sections[1].setCoefficients (matchedZ (f2, f3, 2, sampleRate));
            sections[2].setCoefficients (matchedZ (f4, f4, 0, sampleRate));
            activeSections = 3;
            break;

        case Weighting::c:
            // Four poles: f1 (twice), f4 (twice). Two zeros at DC.
            sections[0].setCoefficients (matchedZ (f1, f1, 2, sampleRate));
            sections[1].setCoefficients (matchedZ (f4, f4, 0, sampleRate));
            activeSections = 2;
            break;

        case Weighting::z:
        default:
            activeSections = 0;
            return;
    }

    // Normalise to unity at 1 kHz. The analogue definition carries an arbitrary scale
    // factor and the discretisation adds its own; both are absorbed here, into the
    // first section, so that the filter reads 0 dB at 1 kHz by construction.
    const double atKilohertz = magnitudeDb (1000.0);
    const double correction  = std::pow (10.0, -atKilohertz / 20.0);

    auto c = sections[0].getCoefficients();
    c.b0 *= correction;
    c.b1 *= correction;
    c.b2 *= correction;
    sections[0].setCoefficients (c);
}

void WeightingFilter::reset() noexcept
{
    for (auto& s : sections)
        s.reset();
}

double WeightingFilter::magnitudeDb (double frequencyHz) const
{
    double sum = 0.0;

    for (std::size_t i = 0; i < activeSections; ++i)
        sum += sections[i].getCoefficients().magnitudeDb (frequencyHz, rate);

    return sum;
}

} // namespace contourtonist::dsp
