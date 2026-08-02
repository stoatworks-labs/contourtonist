// SPDX-License-Identifier: MIT
//
// Tests for the biquad bank. The important ones measure rather than assert: how
// accurately the fit tracks the target curve, and whether the sections it produces are
// stable. A magnitude-fitting routine that quietly produces an unstable filter is the
// worst possible failure for something living on a live output.

#include "../Source/DSP/ContourFilterBank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

void expectNear (double actual, double expected, double tolerance, const std::string& what)
{
    ++checks;
    if (! (std::abs (actual - expected) <= tolerance))
    {
        ++failures;
        std::printf ("  FAIL  %-52s got %9.4f  want %9.4f (tol %.4f)\n",
                     what.c_str(), actual, expected, tolerance);
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

void section (const char* name) { std::printf ("\n%s\n", name); }

/** A biquad with a0 normalised to 1 is stable exactly when its poles are inside the
    unit circle, which for real coefficients is the Jury test: |a2| < 1 and
    |a1| < 1 + a2. */
bool isStable (const contourtonist::dsp::Coefficients& c)
{
    return std::abs (c.a2) < 1.0 && std::abs (c.a1) < 1.0 + c.a2;
}

} // namespace

using namespace contourtonist;
using namespace contourtonist::dsp;

int main()
{
    const std::vector<double> sampleRates { 44100.0, 48000.0, 96000.0, 192000.0 };

    // ---------------------------------------------------------------------------
    section ("A flat target produces a flat bank");

    for (const double fs : sampleRates)
    {
        const auto flat = loudness::compensationCurve (100.0, 100.0);
        const auto bank = fit (flat, fs);

        expectNear (bank.worstErrorDb, 0.0, 1.0e-6,
                    "flat fit is exact at " + std::to_string ((int) fs));

        for (double f = 20.0; f < fs * 0.45; f *= 1.2)
            expectNear (bank.magnitudeDb (f, fs), 0.0, 1.0e-6, "flat bank passes unchanged");
    }

    // ---------------------------------------------------------------------------
    section ("Fit accuracy across the working range of curves");

    // The envelope the controller can actually ask for. Settings::maxGainDb defaults to
    // 12 dB and scales the curve down if it would exceed that, so the bank never sees
    // anything bigger however far the room level falls. Testing curves outside that
    // envelope would be measuring a case the product cannot produce; the extremes are
    // reported separately below instead.
    constexpr double productCeilingDb = 12.0;

    auto clampedCurve = [] (double ref, double cur)
    {
        auto c = loudness::compensationCurve (ref, cur);
        const double peak = c.maxAbsGainDb();

        if (peak > productCeilingDb && peak > 0.0)
            for (auto& g : c.gainDb)
                g *= productCeilingDb / peak;

        return c;
    };

    {
        double worstOverall = 0.0;
        std::string worstCase;

        for (const double fs : sampleRates)
        {
            double worstAtRate = 0.0;

            for (double ref = 60.0; ref <= 105.0; ref += 5.0)
            {
                // Settings::maxTrackingRangeDb caps how far below reference we track.
                for (double cur = ref - 30.0; cur <= ref; cur += 2.5)
                {
                    const auto target = clampedCurve (ref, cur);
                    const auto bank   = fit (target, fs);

                    worstAtRate = std::max (worstAtRate, bank.worstErrorDb);

                    if (bank.worstErrorDb > worstOverall)
                    {
                        worstOverall = bank.worstErrorDb;
                        worstCase = std::to_string ((int) fs) + " Hz, ref "
                                  + std::to_string ((int) ref) + " cur "
                                  + std::to_string ((int) cur)
                                  + " (peak " + std::to_string (target.maxAbsGainDb()) + " dB)";
                    }
                }
            }

            std::printf ("  %6.0f Hz  worst fit error %.4f dB\n", fs, worstAtRate);
        }

        std::printf ("  worst overall: %.4f dB at %s\n", worstOverall, worstCase.c_str());

        expectTrue (worstOverall < 0.50,
                    "fit tracks the target curve to better than 0.50 dB across the "
                    "envelope the controller can produce");
    }

    // ---------------------------------------------------------------------------
    section ("Fit accuracy outside the product envelope, for the record");

    {
        // Not asserted — the controller cannot request these. Recorded so that anyone
        // raising maxGainDb can see what it costs before they do it.
        for (const double peakDb : { 6.0, 12.0, 18.0, 24.0, 34.0 })
        {
            // Find a level pair giving roughly this peak, then fit it unclamped.
            double cur = 100.0;
            while (cur > 20.0
                   && loudness::compensationCurve (100.0, cur).maxAbsGainDb() < peakDb)
                cur -= 0.5;

            const auto target = loudness::compensationCurve (100.0, cur);
            const auto bank   = fit (target, 48000.0);

            std::printf ("  curve peak %5.2f dB  ->  worst fit error %.4f dB\n",
                         target.maxAbsGainDb(), bank.worstErrorDb);
        }
    }

    // ---------------------------------------------------------------------------
    section ("Every section the fit produces is stable");

    {
        std::size_t sectionsChecked = 0;

        for (const double fs : sampleRates)
        {
            for (double ref = 60.0; ref <= 105.0; ref += 5.0)
            {
                for (double cur = 40.0; cur <= ref; cur += 5.0)
                {
                    const auto bank = fit (loudness::compensationCurve (ref, cur), fs);

                    for (std::size_t i = 0; i < numSections; ++i)
                    {
                        ++sectionsChecked;

                        if (! isStable (bank.sections[i]))
                        {
                            expectTrue (false,
                                        "section " + std::to_string (i) + " unstable at "
                                        + std::to_string ((int) fs) + " Hz, ref "
                                        + std::to_string ((int) ref) + " cur "
                                        + std::to_string ((int) cur));
                        }
                    }
                }
            }
        }

        // One check for the whole sweep if it passed, so the count stays readable.
        ++checks;
        std::printf ("  %zu sections checked, all stable\n", sectionsChecked);
    }

    // ---------------------------------------------------------------------------
    section ("The bank does not change level at 1 kHz");

    // The compensation curve is level-neutral by construction; the realisation of it
    // must not throw that away, or the plugin becomes a slow automatic fader.
    for (const double fs : sampleRates)
    {
        for (double cur = 60.0; cur <= 100.0; cur += 10.0)
        {
            const auto bank = fit (loudness::compensationCurve (100.0, cur), fs);
            expectNear (bank.magnitudeDb (1000.0, fs), 0.0, 0.35,
                        "unity at 1 kHz, cur " + std::to_string ((int) cur));
        }
    }

    // ---------------------------------------------------------------------------
    section ("The realised bank adds no latency");

    {
        // An impulse must produce a non-zero first output sample. A cascade of Direct
        // Form I biquads does; anything with lookahead or an FIR delay line would not.
        Processor p;
        p.prepare (1);
        p.setCoefficients (fit (loudness::compensationCurve (100.0, 85.0), 48000.0));

        const double first = p.processSample (0, 1.0);

        expectTrue (std::abs (first) > 1.0e-6,
                    "impulse response is non-zero at sample 0 (no added latency)");
    }

    // ---------------------------------------------------------------------------
    section ("Processing is numerically well behaved");

    {
        Processor p;
        p.prepare (2);
        p.setCoefficients (fit (loudness::compensationCurve (100.0, 70.0), 48000.0));

        // Ten seconds of full-scale noise through both channels.
        std::uint32_t rng = 12345;
        double peak = 0.0;

        for (int n = 0; n < 480000; ++n)
        {
            rng = rng * 1664525u + 1013904223u;
            const double x = ((double) (rng >> 8) / 8388608.0) - 1.0;

            for (std::size_t ch = 0; ch < 2; ++ch)
            {
                const double y = p.processSample (ch, x);
                expectTrue (std::isfinite (y), "output stays finite");
                peak = std::max (peak, std::abs (y));

                if (! std::isfinite (y))
                    return 1;   // no point printing 960000 failures
            }
        }

        std::printf ("  peak output for full-scale noise input: %.3f (%.2f dB)\n",
                     peak, 20.0 * std::log10 (peak));

        // checks was incremented once per sample above; collapse the report.
        std::printf ("  960000 samples, all finite\n");
    }

    // ---------------------------------------------------------------------------
    section ("A steady sine at 1 kHz comes out at the level it went in");

    {
        constexpr double fs = 48000.0;
        Processor p;
        p.prepare (1);
        p.setCoefficients (fit (loudness::compensationCurve (100.0, 80.0), fs));

        // Run past the transient, then measure RMS over a whole number of cycles.
        double sumIn = 0.0, sumOut = 0.0;

        for (int n = 0; n < 96000; ++n)
        {
            const double x = std::sin (2.0 * M_PI * 1000.0 * (double) n / fs);
            const double y = p.processSample (0, x);

            if (n >= 48000)
            {
                sumIn  += x * x;
                sumOut += y * y;
            }
        }

        const double gainDb = 10.0 * std::log10 (sumOut / sumIn);
        std::printf ("  measured 1 kHz gain through the bank: %+.3f dB\n", gainDb);

        expectNear (gainDb, 0.0, 0.35, "1 kHz sine passes at unity");
    }

    // ---------------------------------------------------------------------------
    section ("Fitted response against the target, 100 phon reference at 85");

    {
        constexpr double fs = 48000.0;
        const auto target = loudness::compensationCurve (100.0, 85.0);
        const auto bank   = fit (target, fs);

        for (double f : { 20.0, 31.5, 50.0, 80.0, 125.0, 200.0, 315.0, 500.0, 800.0,
                          1000.0, 1600.0, 2500.0, 4000.0, 6300.0, 10000.0, 12500.0 })
        {
            std::printf ("  %8.1f Hz   target %+6.2f   fitted %+6.2f   err %+.3f\n",
                         f, target.gainAt (f), bank.magnitudeDb (f, fs),
                         bank.magnitudeDb (f, fs) - target.gainAt (f));
        }

        std::printf ("  worst error over the grid: %.4f dB\n", bank.worstErrorDb);
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
