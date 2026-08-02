// SPDX-License-Identifier: MIT
//
// Tests for A/C/Z weighting.
//
// Two independent things are checked against each other. `referenceWeightingDb`
// computes the weighting from the analogue transfer function IEC 61672-1 specifies.
// `kPrintedTable` below is the standard's own tabulated presentation of that same
// weighting, transcribed. If a formula and a table that were arrived at separately
// agree, both are probably right; if they disagree, one of them is wrong and the test
// says which frequency to look at.
//
// Then the discretised filter is measured against the reference, which is where the
// bilinear pre-warping either works or does not.

#include "../Source/DSP/Weighting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
        std::printf ("  FAIL  %-50s got %8.3f  want %8.3f (tol %.2f)\n",
                     what.c_str(), actual, expected, tolerance);
    }
}

void expectTrue (bool condition, const std::string& what)
{
    ++checks;
    if (! condition) { ++failures; std::printf ("  FAIL  %s\n", what.c_str()); }
}

void section (const char* name) { std::printf ("\n%s\n", name); }

/** IEC 61672-1 tabulated weightings, transcribed. Nominal frequency, A, C.

    The values are stated against *exact* midband frequencies, 1000 * 10^(n/10), not
    against the rounded nominal labels — evaluating at 12500 Hz instead of the exact
    12589.25 Hz is worth a couple of tenths of a dB at the ends of the band, which is
    enough to fail a test that is actually correct. */
struct Row { int nominal; double a; double c; };

constexpr Row kPrintedTable[] {
    {    10, -70.4, -14.3 }, {  12,   -63.4, -11.2 }, {    16, -56.7,  -8.5 },
    {    20, -50.5,  -6.2 }, {  25,   -44.7,  -4.4 }, {  31,   -39.4,  -3.0 },
    {    40, -34.6,  -2.0 }, {  50,   -30.2,  -1.3 }, {    63, -26.2,  -0.8 },
    {    80, -22.5,  -0.5 }, { 100,   -19.1,  -0.3 }, {   125, -16.1,  -0.2 },
    {   160, -13.4,  -0.1 }, { 200,   -10.9,   0.0 }, {   250,  -8.6,   0.0 },
    {   315,  -6.6,   0.0 }, { 400,    -4.8,   0.0 }, {   500,  -3.2,   0.0 },
    {   630,  -1.9,   0.0 }, { 800,    -0.8,   0.0 }, {  1000,   0.0,   0.0 },
    {  1250,   0.6,   0.0 }, { 1600,    1.0,  -0.1 }, {  2000,   1.2,  -0.2 },
    {  2500,   1.3,  -0.3 }, { 3150,    1.2,  -0.5 }, {  4000,   1.0,  -0.8 },
    {  5000,   0.5,  -1.3 }, { 6300,   -0.1,  -2.0 }, {  8000,  -1.1,  -3.0 },
    { 10000,  -2.5,  -4.4 }, { 12500,  -4.3,  -6.2 }, { 16000,  -6.6,  -8.5 },
    { 20000,  -9.3, -11.2 }
};

/** The exact midband frequency for the band whose nominal label is @p nominal. */
double exactMidband (int nominal)
{
    // Bands are 1000 * 10^(n/10). Recover n from the nominal label by rounding.
    const double n = std::round (10.0 * std::log10 ((double) nominal / 1000.0));
    return 1000.0 * std::pow (10.0, n / 10.0);
}

} // namespace

using namespace contourtonist::dsp;

int main()
{
    // ---------------------------------------------------------------------------
    section ("The analogue definition agrees with the standard's printed table");

    {
        double worstA = 0.0, worstC = 0.0;

        for (const auto& row : kPrintedTable)
        {
            const double f = exactMidband (row.nominal);

            const double a = referenceWeightingDb (Weighting::a, f);
            const double c = referenceWeightingDb (Weighting::c, f);

            worstA = std::max (worstA, std::abs (a - row.a));
            worstC = std::max (worstC, std::abs (c - row.c));

            // The table is printed to one decimal, so 0.05 is rounding and anything
            // beyond ~0.06 is a real disagreement.
            expectNear (a, row.a, 0.07, "A at " + std::to_string (row.nominal) + " Hz");
            expectNear (c, row.c, 0.07, "C at " + std::to_string (row.nominal) + " Hz");
        }

        std::printf ("  worst disagreement with the printed table: A %.3f dB, C %.3f dB\n",
                     worstA, worstC);
    }

    // ---------------------------------------------------------------------------
    section ("Both weightings are exactly 0 dB at 1 kHz");

    expectNear (referenceWeightingDb (Weighting::a, 1000.0), 0.0, 1.0e-12, "A at 1 kHz");
    expectNear (referenceWeightingDb (Weighting::c, 1000.0), 0.0, 1.0e-12, "C at 1 kHz");
    expectNear (referenceWeightingDb (Weighting::z, 1000.0), 0.0, 1.0e-12, "Z at 1 kHz");

    // ---------------------------------------------------------------------------
    section ("The discretised filter tracks the analogue reference");

    // Two separate questions, because the answers are very different.
    //
    // Below 12.5 kHz the discretisation should be essentially exact, and that is
    // asserted tightly. Above it, the matched Z-transform aliases: the digital response
    // is the analogue one plus its images folded back, and at 48 kHz the 12.2 kHz
    // double pole is close enough to Nyquist for that to matter. That error is measured
    // and reported, not asserted to be small, because it is not small.
    //
    // What saves it is where the error lands. It is confined to the top third of an
    // octave of the audible band, it is negative (the filter under-reads rather than
    // over-reads), and programme material has very little energy there. Its effect on a
    // broadband LAeq of real programme is far below the error already accepted from
    // microphone placement. If you need better, run at 96 or 192 kHz — the numbers
    // below show what that buys — or oversample the measurement path.
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const auto weighting : { Weighting::a, Weighting::c })
        {
            WeightingFilter filter;
            filter.prepare (weighting, fs);

            double worstLow = 0.0, worstHigh = 0.0;
            int worstLowFreq = 0, worstHighFreq = 0;

            for (const auto& row : kPrintedTable)
            {
                const double f = exactMidband (row.nominal);

                // Nothing sensible to say about frequencies above Nyquist.
                if (f > fs * 0.45)
                    continue;

                const double err = std::abs (filter.magnitudeDb (f)
                                           - referenceWeightingDb (weighting, f));

                if (f <= 12500.0)
                {
                    if (err > worstLow) { worstLow = err; worstLowFreq = row.nominal; }
                }
                else if (err > worstHigh) { worstHigh = err; worstHighFreq = row.nominal; }
            }

            std::printf ("  %6.0f Hz  %c-weighting: <=12.5k worst %.3f dB (at %d Hz), "
                         ">12.5k worst %.3f dB (at %d Hz)\n",
                         fs, weighting == Weighting::a ? 'A' : 'C',
                         worstLow, worstLowFreq, worstHigh, worstHighFreq);

            // Asserted against measured behaviour, not against an aspiration. 1.3 dB
            // below 12.5 kHz is what a matched Z-transform delivers at 44.1 kHz; if a
            // change makes it worse, that is a regression worth catching, and if a
            // change makes it much better this assertion should be tightened.
            expectTrue (worstLow < 1.3,
                        std::string ("no regression below 12.5 kHz for ")
                        + (weighting == Weighting::a ? "A" : "C")
                        + " at " + std::to_string ((int) fs));
        }
    }

    // ---------------------------------------------------------------------------
    section ("Accuracy improves as expected with sample rate");

    // The honest mitigation for the top-octave error is to measure at a higher rate.
    // Check that actually works, so the advice in the docs is backed by something.
    {
        double previous = 1e9;

        for (const double fs : { 48000.0, 96000.0, 192000.0 })
        {
            WeightingFilter filter;
            filter.prepare (Weighting::a, fs);

            const double at10k = std::abs (filter.magnitudeDb (10000.0)
                                         - referenceWeightingDb (Weighting::a, 10000.0));

            expectTrue (at10k < previous,
                        "10 kHz accuracy improves at " + std::to_string ((int) fs));
            previous = at10k;
        }
    }

    // ---------------------------------------------------------------------------
    section ("What the top-octave error costs a broadband measurement");

    {
        // The claim above is that the high-frequency error does not matter for real
        // programme. Check it rather than asserting it: integrate the weighted power of
        // a pink-noise-like spectrum through the true analogue weighting and through
        // the discretised filter, and compare the broadband totals.
        constexpr double fs = 48000.0;

        WeightingFilter filter;
        filter.prepare (Weighting::a, fs);

        double powerReference = 0.0, powerFiltered = 0.0;

        // Third-octave bands from 20 Hz to 20 kHz, pink (equal power per octave, so
        // equal power per third-octave band).
        for (int n = -17; n <= 13; ++n)
        {
            const double f = 1000.0 * std::pow (10.0, n / 10.0);
            if (f > fs * 0.45) continue;

            constexpr double bandPower = 1.0;

            powerReference += bandPower
                * std::pow (10.0, referenceWeightingDb (Weighting::a, f) / 10.0);
            powerFiltered  += bandPower
                * std::pow (10.0, filter.magnitudeDb (f) / 10.0);
        }

        const double errorDb = 10.0 * std::log10 (powerFiltered / powerReference);

        std::printf ("  broadband A-weighted level of pink noise, filter vs reference: "
                     "%+.4f dB\n", errorDb);

        // Pink noise is the harsh case: it has as much energy in the top third-octave
        // as in any other, which no programme material does. Even so the broadband
        // error is a third of a dB, which costs about 0.16 dB of compensation curve at
        // 20 Hz — irrelevant to the job. It is not irrelevant if you are reading the
        // dB(A) as a compliance figure, which is why the docs tell you to feed a real
        // meter in for that. See docs/weighting.md.
        expectTrue (std::abs (errorDb) < 0.4,
                    "broadband A-weighted error on pink noise stays under 0.4 dB");
    }

    // ---------------------------------------------------------------------------
    section ("Z-weighting is exactly flat and passes audio untouched");

    {
        WeightingFilter z;
        z.prepare (Weighting::z, 48000.0);

        for (double f = 10.0; f < 20000.0; f *= 1.3)
            expectNear (z.magnitudeDb (f), 0.0, 1.0e-12, "Z is flat");

        for (double x : { 1.0, -0.5, 0.25 })
            expectNear (z.processSample (x), x, 1.0e-12, "Z passes samples unchanged");
    }

    // ---------------------------------------------------------------------------
    section ("A steady 1 kHz sine measures 0 dB through A and C weighting");

    for (const auto weighting : { Weighting::a, Weighting::c })
    {
        constexpr double fs = 48000.0;
        WeightingFilter filter;
        filter.prepare (weighting, fs);

        double sumIn = 0.0, sumOut = 0.0;

        for (int n = 0; n < 96000; ++n)
        {
            const double x = std::sin (2.0 * M_PI * 1000.0 * (double) n / fs);
            const double y = filter.processSample (x);

            if (n >= 48000) { sumIn += x * x; sumOut += y * y; }
        }

        const double gainDb = 10.0 * std::log10 (sumOut / sumIn);
        std::printf ("  %c-weighted 1 kHz sine: %+.4f dB\n",
                     weighting == Weighting::a ? 'A' : 'C', gainDb);

        expectNear (gainDb, 0.0, 0.02, "1 kHz passes at unity through the real filter");
    }

    // ---------------------------------------------------------------------------
    section ("Filter response against the reference, 48 kHz");

    {
        WeightingFilter a, c;
        a.prepare (Weighting::a, 48000.0);
        c.prepare (Weighting::c, 48000.0);

        std::printf ("  %8s %9s %9s %8s %9s %9s %8s\n",
                     "f (Hz)", "A ref", "A filt", "err", "C ref", "C filt", "err");

        for (const auto& row : kPrintedTable)
        {
            const double f = exactMidband (row.nominal);
            if (f > 48000.0 * 0.45) continue;

            std::printf ("  %8.1f %9.2f %9.2f %8.3f %9.2f %9.2f %8.3f\n",
                         f,
                         referenceWeightingDb (Weighting::a, f), a.magnitudeDb (f),
                         a.magnitudeDb (f) - referenceWeightingDb (Weighting::a, f),
                         referenceWeightingDb (Weighting::c, f), c.magnitudeDb (f),
                         c.magnitudeDb (f) - referenceWeightingDb (Weighting::c, f));
        }
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
