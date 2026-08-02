// SPDX-License-Identifier: MIT
#include "EqualLoudness.h"

#include <algorithm>
#include <cmath>

namespace contourtonist::loudness
{
namespace
{

// ---------------------------------------------------------------------------------
// ISO 226:2003 Table 1 — "Parameters of Equation (1) used to calculate the normal
// equal-loudness-level contours". Transcribed from the standard itself.
//
// If you ever need to re-verify these: the sharpest single check is that
// soundPressureLevel(1000, P) == P for every P, which exercises af, Lu and Tf at
// 1 kHz simultaneously and fails loudly if any one of them is wrong.
// ---------------------------------------------------------------------------------

constexpr std::array<double, numTabulated> kFrequency {
     20.0,    25.0,    31.5,    40.0,    50.0,    63.0,    80.0,   100.0,   125.0,
    160.0,   200.0,   250.0,   315.0,   400.0,   500.0,   630.0,   800.0,  1000.0,
   1250.0,  1600.0,  2000.0,  2500.0,  3150.0,  4000.0,  5000.0,  6300.0,  8000.0,
  10000.0, 12500.0
};

// af — exponent for loudness perception.
constexpr std::array<double, numTabulated> kAlphaF {
    0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349,
    0.330, 0.315, 0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250,
    0.246, 0.244, 0.243, 0.243, 0.243, 0.242, 0.242, 0.245, 0.254,
    0.271, 0.301
};

// Lu — magnitude of the linear transfer function normalised at 1 kHz, in dB.
constexpr std::array<double, numTabulated> kLu {
    -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3,  -8.1,  -6.2,
     -4.5,  -3.1,  -2.0,  -1.1,  -0.4,   0.0,   0.3,   0.5,   0.0,
     -2.7,  -4.1,  -1.0,   1.7,   2.5,   1.2,  -2.1,  -7.1, -11.2,
    -10.7,  -3.1
};

// Tf — threshold of hearing, in dB.
constexpr std::array<double, numTabulated> kTf {
     78.5,  68.7,  59.5,  51.1,  44.0,  37.5,  31.5,  26.5,  22.1,
     17.9,  14.4,  11.4,   8.6,   6.2,   4.4,   3.0,   2.2,   2.4,
      3.5,   1.7,  -1.3,  -4.2,  -6.0,  -5.4,  -1.5,   6.0,  12.6,
     13.9,  12.3
};

/** The 0.005 135 constant of equation (2). It is the value the threshold term takes
    at 1 kHz, which is what makes the inverse return exactly 0 phon at the 1 kHz
    threshold. */
constexpr double kBfOffset = 0.005135;

/** Af and Bf are arguments to a logarithm. The equations cannot produce a
    non-positive value anywhere inside — or near — their stated range, but a live PA is
    no place to find out that an edge case makes a NaN, so both are floored. The floor
    is far below any level anyone can hear. */
constexpr double kLogArgumentFloor = 1.0e-12;

/** Equation (1) evaluated with the Table 1 coefficients of one tabulated frequency. */
double splAtIndex (std::size_t i, double phon)
{
    const double af = kAlphaF[i];
    const double lu = kLu[i];
    const double tf = kTf[i];

    const double threshold = std::pow (0.4 * std::pow (10.0, ((tf + lu) / 10.0) - 9.0), af);
    const double af_ = 4.47e-3 * (std::pow (10.0, 0.025 * phon) - 1.15) + threshold;

    return (10.0 / af) * std::log10 (std::max (af_, kLogArgumentFloor)) - lu + 94.0;
}

/** Equation (2) evaluated with the Table 1 coefficients of one tabulated frequency. */
double phonAtIndexStandard (std::size_t i, double splDb)
{
    const double af = kAlphaF[i];
    const double lu = kLu[i];
    const double tf = kTf[i];

    const double signal    = std::pow (0.4 * std::pow (10.0, ((splDb + lu) / 10.0) - 9.0), af);
    const double threshold = std::pow (0.4 * std::pow (10.0, ((tf   + lu) / 10.0) - 9.0), af);
    const double bf        = signal - threshold + kBfOffset;

    return 40.0 * std::log10 (std::max (bf, kLogArgumentFloor)) + 94.0;
}

/** The algebraic inverse of equation (1), which is not quite equation (2).

    Rearranging eq. (1) for Ln:

        Af = 10^(af * (Lp + Lu - 94) / 10)                    from Lp
        10^(0.025*Ln) = (Af - threshold) / 4.47e-3 + 1.15     from the definition of Af
        Ln = 40 * lg((Af - threshold) / 4.47e-3 + 1.15)

    Below the threshold of hearing the bracket goes non-positive and no loudness level
    exists; the caller gets the floor rather than a NaN.
*/
double phonAtIndexExact (std::size_t i, double splDb)
{
    const double af = kAlphaF[i];
    const double lu = kLu[i];
    const double tf = kTf[i];

    const double threshold = std::pow (0.4 * std::pow (10.0, ((tf + lu) / 10.0) - 9.0), af);
    const double afTerm    = std::pow (10.0, af * (splDb + lu - 94.0) / 10.0);
    const double bracket   = (afTerm - threshold) / 4.47e-3 + 1.15;

    return 40.0 * std::log10 (std::max (bracket, kLogArgumentFloor));
}

double phonAtIndex (std::size_t i, double splDb, InverseMethod method)
{
    return method == InverseMethod::exact ? phonAtIndexExact (i, splDb)
                                          : phonAtIndexStandard (i, splDb);
}

/** Position of @p frequencyHz within the tabulated frequencies, as an index pair and a
    fraction for interpolating in log-frequency. Frequencies outside the table clamp to
    its ends — the table spans 20 Hz to 12.5 kHz, and there is nothing sensible to say
    about equal loudness beyond that. */
struct LogInterp
{
    std::size_t lower = 0;
    std::size_t upper = 0;
    double fraction = 0.0;
};

LogInterp locate (double frequencyHz)
{
    LogInterp r;

    if (frequencyHz <= kFrequency.front())
        return { 0, 0, 0.0 };

    if (frequencyHz >= kFrequency.back())
        return { numTabulated - 1, numTabulated - 1, 0.0 };

    std::size_t i = 0;
    while (i + 1 < numTabulated && kFrequency[i + 1] < frequencyHz)
        ++i;

    r.lower = i;
    r.upper = i + 1;

    const double lo = std::log (kFrequency[r.lower]);
    const double hi = std::log (kFrequency[r.upper]);
    r.fraction = (std::log (frequencyHz) - lo) / (hi - lo);

    return r;
}

/** The upper validity limit of equation (1) at a given frequency, in phons. */
double validCeiling (double frequencyHz)
{
    return frequencyHz >= hfCeilingFrequency ? validMaxPhonHF : validMaxPhonLF;
}

/** Apply the extrapolation policy to a loudness level.

    Note the ceiling used is deliberately the *low frequency* one (90 phon) at every
    frequency, not the per-frequency one. Clamping HF to 80 while LF is free to reach
    90 would put a step in the contour between 4 kHz and 5 kHz, and a step in the
    contour is a step in the EQ curve — an audible artefact invented by the
    implementation rather than by the standard. A single global limit keeps the curve
    smooth. isWithinValidRange() still reports the true per-frequency limit, so the
    extrapolation flag is set honestly for 5 kHz and above between 80 and 90 phon.
*/
double applyPolicy (double phon, ExtrapolationPolicy policy)
{
    if (policy == ExtrapolationPolicy::extend)
        return phon;

    return std::clamp (phon, validMinPhon, validMaxPhonLF);
}

} // namespace

const std::array<double, numTabulated>& tabulatedFrequencies()
{
    return kFrequency;
}

bool isWithinValidRange (double frequencyHz, double phon)
{
    return phon >= validMinPhon && phon <= validCeiling (frequencyHz);
}

double soundPressureLevel (double frequencyHz, double phon, ExtrapolationPolicy policy)
{
    const double p = applyPolicy (phon, policy);
    const auto at = locate (frequencyHz);

    if (at.lower == at.upper)
        return splAtIndex (at.lower, p);

    const double lo = splAtIndex (at.lower, p);
    const double hi = splAtIndex (at.upper, p);

    return lo + at.fraction * (hi - lo);
}

double loudnessLevel (double frequencyHz, double splDb, ExtrapolationPolicy policy,
                      InverseMethod method)
{
    const auto at = locate (frequencyHz);

    double phon;

    if (at.lower == at.upper)
    {
        phon = phonAtIndex (at.lower, splDb, method);
    }
    else
    {
        const double lo = phonAtIndex (at.lower, splDb, method);
        const double hi = phonAtIndex (at.upper, splDb, method);
        phon = lo + at.fraction * (hi - lo);
    }

    return applyPolicy (phon, policy);
}

double Contour::maxAbsGainDb() const
{
    double m = 0.0;

    for (const double g : gainDb)
        m = std::max (m, std::abs (g));

    return m;
}

double Contour::gainAt (double frequencyHz) const
{
    const auto at = locate (frequencyHz);

    if (at.lower == at.upper)
        return gainDb[at.lower];

    return gainDb[at.lower] + at.fraction * (gainDb[at.upper] - gainDb[at.lower]);
}

Contour compensationCurve (double referencePhon, double currentPhon,
                           ExtrapolationPolicy policy)
{
    Contour c;
    c.frequencies    = &kFrequency;
    c.referencePhon  = referencePhon;
    c.currentPhon    = currentPhon;

    const double ref = applyPolicy (referencePhon, policy);
    const double cur = applyPolicy (currentPhon,   policy);

    // The broadband shift the material has already been subjected to.
    const double broadband = ref - cur;

    for (std::size_t i = 0; i < numTabulated; ++i)
    {
        // What the contours say the shift at this frequency *should* have been.
        const double required = splAtIndex (i, ref) - splAtIndex (i, cur);

        c.gainDb[i] = broadband - required;

        if (! isWithinValidRange (kFrequency[i], referencePhon)
         || ! isWithinValidRange (kFrequency[i], currentPhon))
            c.extrapolated = true;
    }

    // G(1 kHz) is zero analytically; force it so that floating point cannot leave a
    // few thousandths of a dB of broadband gain behind. Everything downstream relies
    // on this curve being level-neutral.
    c.gainDb[17] = 0.0;

    return c;
}

} // namespace contourtonist::loudness
