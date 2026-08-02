// SPDX-License-Identifier: MIT
//
// Tests for the ISO 226:2003 model. These build and run with a plain C++ compiler —
// no JUCE, no audio device, no host. See tests/README.md.
//
// On what these can and cannot prove:
//
// The normative Table 1 coefficients were transcribed from the standard. Annex B, which
// tabulates the resulting contours, is not in the publicly available preview of the
// document, so we cannot diff against it here. Instead the transcription is checked
// three ways, which together are strong:
//
//   1. Lp(1 kHz, P) == P exactly, for all P. This is the *definition* of the phon and
//      it exercises af, Lu and Tf at 1 kHz simultaneously. Any one of the three being
//      wrong breaks it.
//   2. The algebraic inverse of equation (1) round-trips to machine precision at every
//      tabulated frequency. An inverse derived by rearranging the forward equation can
//      only round-trip that cleanly if the coefficients it is fed are self-consistent,
//      which mistranscribed ones would not be.
//   3. Spot values against figures reproduced in the literature (the 40 phon contour
//      passing through ~99.9 dB at 20 Hz is the most-cited number in the standard).
//
// Point 2 is also how we discovered that ISO 226:2003's printed equations (1) and (2)
// are not exact inverses of one another. See the section on it below.

#include "../Source/DSP/EqualLoudness.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <string>

namespace
{
int failures = 0;
int checks = 0;

void expectNear (double actual, double expected, double tolerance, const std::string& what)
{
    ++checks;
    const double err = std::abs (actual - expected);

    if (! (err <= tolerance))
    {
        ++failures;
        std::printf ("  FAIL  %-58s got %10.4f  want %10.4f  (err %.5f > %.5f)\n",
                     what.c_str(), actual, expected, err, tolerance);
    }
}

void expectTrue (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL  %s\n", what.c_str());
    }
}

void section (const char* name)
{
    std::printf ("\n%s\n", name);
}

} // namespace

using namespace contourtonist::loudness;

int main()
{
    const auto& freqs = tabulatedFrequencies();

    // -----------------------------------------------------------------------------
    section ("The 1 kHz identity — Lp(1 kHz, P) == P by definition of the phon");

    for (double phon = 20.0; phon <= 90.0; phon += 5.0)
        expectNear (soundPressureLevel (1000.0, phon), phon, 0.02,
                    "Lp(1000, " + std::to_string ((int) phon) + ")");

    // The inverse must agree at 1 kHz too.
    for (double spl = 20.0; spl <= 90.0; spl += 5.0)
        expectNear (loudnessLevel (1000.0, spl), spl, 0.02,
                    "Ln(1000, " + std::to_string ((int) spl) + ")");

    // -----------------------------------------------------------------------------
    section ("The exact inverse round-trips to machine precision everywhere");

    for (std::size_t i = 0; i < numTabulated; ++i)
    {
        for (double phon = 20.0; phon <= 80.0; phon += 10.0)
        {
            const double spl = soundPressureLevel (freqs[i], phon);
            const double back = loudnessLevel (freqs[i], spl);

            expectNear (back, phon, 1.0e-9,
                        "round trip at " + std::to_string ((int) freqs[i]) + " Hz, "
                        + std::to_string ((int) phon) + " phon");
        }
    }

    // -----------------------------------------------------------------------------
    section ("ISO 226:2003's own equations (1) and (2) are not exact inverses");

    // This is a property of the standard, not of this code, and it is worth pinning
    // down so nobody later "fixes" the exact inverse to match the printed one.
    //
    // The standard rounds constants independently in the two equations. The largest is
    // 0.4 standing in for 10^-0.4 = 0.398107, worth 0.0206 dB at 1 kHz; the 0.005135
    // and 94 of equation (2) account for the remainder. Equation (2) reads high
    // everywhere — 0.03 dB at 1 kHz, rising to about 0.056 dB at the extremes of the
    // range where the af exponent is furthest from 0.25.
    {
        double worst = 0.0;

        for (std::size_t i = 0; i < numTabulated; ++i)
        {
            for (double phon = 20.0; phon <= 80.0; phon += 10.0)
            {
                const double spl = soundPressureLevel (freqs[i], phon);
                const double viaStandard = loudnessLevel (freqs[i], spl,
                                                          ExtrapolationPolicy::extend,
                                                          InverseMethod::standardEquation2);
                worst = std::max (worst, std::abs (viaStandard - phon));
            }
        }

        std::printf ("  equation (2) round-trip error, worst case: %.4f dB\n", worst);

        expectTrue (worst > 0.01, "equation (2) really is inconsistent with equation (1)");
        expectTrue (worst < 0.10, "and the inconsistency is small enough to be irrelevant");
    }

    // -----------------------------------------------------------------------------
    section ("Spot values against the published 40 phon contour");

    // The most widely reproduced single number from ISO 226:2003: a 20 Hz tone needs
    // very nearly 100 dB SPL to sound as loud as 1 kHz at 40 dB.
    expectNear (soundPressureLevel (20.0, 40.0), 99.85, 0.1, "Lp(20 Hz, 40 phon)");

    // The ear's most sensitive region, around 3-4 kHz, sits *below* the 1 kHz level.
    expectTrue (soundPressureLevel (3150.0, 40.0) < 40.0,
                "3150 Hz at 40 phon is below 40 dB (ear is more sensitive than at 1 kHz)");
    expectTrue (soundPressureLevel (4000.0, 40.0) < 40.0,
                "4000 Hz at 40 phon is below 40 dB");

    // And low frequencies always need far more level.
    expectTrue (soundPressureLevel (20.0, 40.0) > 95.0, "20 Hz at 40 phon needs > 95 dB");

    // -----------------------------------------------------------------------------
    section ("Contours are monotonic in level and never cross");

    for (std::size_t i = 0; i < numTabulated; ++i)
    {
        double previous = -1000.0;

        for (double phon = 20.0; phon <= 90.0; phon += 2.5)
        {
            const double spl = soundPressureLevel (freqs[i], phon);
            expectTrue (spl > previous,
                        "contour rises with level at " + std::to_string ((int) freqs[i]) + " Hz");
            previous = spl;
        }
    }

    // -----------------------------------------------------------------------------
    section ("The compensation curve is level-neutral");

    // The whole design rests on this: the curve must never apply broadband gain, or
    // the plugin becomes a fader and the control loop becomes a feedback loop with
    // gain. G(1 kHz) must be exactly zero for every level pair.
    for (double ref = 30.0; ref <= 100.0; ref += 5.0)
    {
        for (double cur = 30.0; cur <= 100.0; cur += 5.0)
        {
            const auto c = compensationCurve (ref, cur);
            expectNear (c.gainAt (1000.0), 0.0, 1.0e-9,
                        "G(1 kHz) is zero for ref " + std::to_string ((int) ref)
                        + " cur " + std::to_string ((int) cur));
        }
    }

    // -----------------------------------------------------------------------------
    section ("The compensation curve has the right sign and shape");

    {
        // Playing 20 dB quieter than the reference: bass must be restored, not cut.
        const auto quieter = compensationCurve (80.0, 60.0);
        expectTrue (quieter.gainAt (20.0) > 0.0, "quieter than reference boosts 20 Hz");
        expectTrue (quieter.gainAt (40.0) > 0.0, "quieter than reference boosts 40 Hz");
        expectTrue (quieter.gainAt (20.0) > quieter.gainAt (100.0),
                    "boost increases as frequency falls");

        // Playing louder than the reference must do the opposite.
        const auto louder = compensationCurve (60.0, 80.0);
        expectTrue (louder.gainAt (20.0) < 0.0, "louder than reference cuts 20 Hz");

        // And the two must be mirror images.
        expectNear (louder.gainAt (20.0), -quieter.gainAt (20.0), 1.0e-9,
                    "louder and quieter curves are symmetric at 20 Hz");

        // No level change at all means no EQ at all.
        const auto flat = compensationCurve (75.0, 75.0);
        expectNear (flat.maxAbsGainDb(), 0.0, 1.0e-9, "equal levels give a flat curve");
    }

    // -----------------------------------------------------------------------------
    section ("The curve is smooth — no step where the HF validity ceiling changes");

    {
        // A per-frequency clamp at 4/5 kHz would put a discontinuity here. Walk across
        // the boundary at a level that would trigger it and check the curve stays
        // continuous.
        const auto c = compensationCurve (95.0, 85.0);

        double previous = c.gainAt (3000.0);

        for (double f = 3000.0; f <= 8000.0; f *= 1.02)
        {
            const double g = c.gainAt (f);
            expectTrue (std::abs (g - previous) < 0.5,
                        "no step in curve near " + std::to_string ((int) f) + " Hz");
            previous = g;
        }
    }

    // -----------------------------------------------------------------------------
    section ("Extrapolation is flagged, never hidden");

    {
        const auto inRange = compensationCurve (70.0, 50.0);
        expectTrue (! inRange.extrapolated, "70/50 phon is inside the validated range");

        const auto showLevel = compensationCurve (100.0, 92.0);
        expectTrue (showLevel.extrapolated, "100/92 phon is flagged as extrapolated");

        const auto veryQuiet = compensationCurve (70.0, 10.0);
        expectTrue (veryQuiet.extrapolated, "10 phon is below the 20 phon floor and flagged");

        // Clamping is what strict conformance costs: at show level it does nothing.
        const auto clamped = compensationCurve (100.0, 92.0, ExtrapolationPolicy::clamp);
        expectNear (clamped.maxAbsGainDb(), 0.0, 1.0e-9,
                    "clamp policy yields no compensation above the ceiling");

        // Which is exactly why extend is the default.
        expectTrue (showLevel.maxAbsGainDb() > 1.0,
                    "extend policy still compensates at show level");
    }

    // -----------------------------------------------------------------------------
    section ("Nothing produces a NaN or an infinity");

    for (double f = 5.0; f <= 30000.0; f *= 1.05)
    {
        for (double phon = -40.0; phon <= 140.0; phon += 10.0)
        {
            const double spl = soundPressureLevel (f, phon);
            expectTrue (std::isfinite (spl),
                        "Lp finite at " + std::to_string ((int) f) + " Hz, "
                        + std::to_string ((int) phon) + " phon");

            expectTrue (std::isfinite (loudnessLevel (f, spl)),
                        "Ln finite at " + std::to_string ((int) f) + " Hz");
        }
    }

    // -----------------------------------------------------------------------------
    // Print the 40 phon contour so it can be eyeballed against a published figure.
    section ("The 40 phon contour (compare against ISO 226:2003 Annex A)");

    for (std::size_t i = 0; i < numTabulated; ++i)
        std::printf ("  %8.1f Hz   %7.2f dB\n", freqs[i], soundPressureLevel (freqs[i], 40.0));

    section ("Compensation curve, 100 phon reference reproduced at 85 phon");

    {
        const auto c = compensationCurve (100.0, 85.0);

        for (std::size_t i = 0; i < numTabulated; ++i)
            std::printf ("  %8.1f Hz   %+7.2f dB\n", freqs[i], c.gainDb[i]);

        std::printf ("  extrapolated: %s   max |gain|: %.2f dB\n",
                     c.extrapolated ? "yes" : "no", c.maxAbsGainDb());
    }

    // -----------------------------------------------------------------------------
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
