// SPDX-License-Identifier: MIT
//
// Tests for the sliding Leq integrator.

#include "../Source/DSP/ShortTermLeq.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0, checks = 0;

void expectNear (double actual, double expected, double tol, const std::string& what)
{
    ++checks;
    if (! (std::abs (actual - expected) <= tol))
    {
        ++failures;
        std::printf ("  FAIL  %-50s got %9.4f want %9.4f (tol %.3f)\n",
                     what.c_str(), actual, expected, tol);
    }
}

void expectTrue (bool c, const std::string& what)
{
    ++checks;
    if (! c) { ++failures; std::printf ("  FAIL  %s\n", what.c_str()); }
}

void section (const char* n) { std::printf ("\n%s\n", n); }

/** Feed @p seconds of a sine at @p amplitude. */
void feedSine (contourtonist::dsp::ShortTermLeq& leq, double amplitude, double freq,
               double seconds, double fs, double phaseStart = 0.0)
{
    const auto n = (std::size_t) (seconds * fs);
    for (std::size_t i = 0; i < n; ++i)
        leq.push (amplitude * std::sin (2.0 * M_PI * freq * ((double) i / fs) + phaseStart));
}

} // namespace

using namespace contourtonist::dsp;

int main()
{
    constexpr double fs = 48000.0;

    // ---------------------------------------------------------------------------
    section ("A full-scale sine reads -3.01 dBFS");

    {
        // RMS of a unit sine is 1/sqrt(2), which is -3.0103 dB relative to RMS 1.0.
        ShortTermLeq leq;
        leq.prepare (1.0, fs, Weighting::z);
        feedSine (leq, 1.0, 1000.0, 3.0, fs);

        expectNear (leq.currentLevelDb(), -3.0103, 0.01, "unit sine is -3.01 dBFS");
        expectTrue (leq.isPrimed(), "window is primed after 3 s of a 1 s window");
    }

    // ---------------------------------------------------------------------------
    section ("Halving amplitude drops the level by 6.02 dB");

    {
        ShortTermLeq a, b;
        a.prepare (1.0, fs, Weighting::z);
        b.prepare (1.0, fs, Weighting::z);

        feedSine (a, 1.0, 1000.0, 3.0, fs);
        feedSine (b, 0.5, 1000.0, 3.0, fs);

        expectNear (a.currentLevelDb() - b.currentLevelDb(), 6.0206, 0.01,
                    "half amplitude is 6.02 dB down");
    }

    // ---------------------------------------------------------------------------
    section ("Calibration turns dBFS into dB SPL");

    {
        ShortTermLeq leq;
        leq.prepare (1.0, fs, Weighting::z);

        expectTrue (! leq.isCalibrated(), "starts uncalibrated");

        feedSine (leq, 1.0, 1000.0, 3.0, fs);
        const double dbfs = leq.currentLevelDb();

        // A calibrator producing 94 dB SPL that reads -20 dBFS gives a +114 dB offset.
        leq.setCalibration (114.0);

        expectTrue (leq.isCalibrated(), "reports calibrated");
        expectNear (leq.currentLevelDb(), dbfs + 114.0, 1.0e-9,
                    "level shifts by exactly the calibration offset");

        leq.clearCalibration();
        expectNear (leq.currentLevelDb(), dbfs, 1.0e-9, "clearing restores dBFS");
    }

    // ---------------------------------------------------------------------------
    section ("The window really is a window: old energy leaves it");

    {
        ShortTermLeq leq;
        leq.prepare (2.0, fs, Weighting::z);

        // Two seconds of loud, then four seconds of quiet. After the window has passed
        // over, the loud part must be entirely gone — an exponential average would
        // still be carrying it.
        feedSine (leq, 1.0, 1000.0, 2.0, fs);
        const double loud = leq.currentLevelDb();

        feedSine (leq, 0.01, 1000.0, 4.0, fs);
        const double afterwards = leq.currentLevelDb();

        std::printf ("  loud %.2f dBFS, four seconds later %.2f dBFS\n", loud, afterwards);

        expectNear (loud, -3.0103, 0.05, "loud section reads full scale");
        expectNear (afterwards, -3.0103 - 40.0, 0.1,
                    "quiet section reads 40 dB down, with no residue of the loud one");
    }

    // ---------------------------------------------------------------------------
    section ("Priming is reported honestly");

    {
        ShortTermLeq leq;
        leq.prepare (5.0, fs, Weighting::z);

        feedSine (leq, 1.0, 1000.0, 1.0, fs);
        expectTrue (! leq.isPrimed(), "not primed after 1 s of a 5 s window");

        feedSine (leq, 1.0, 1000.0, 4.5, fs);
        expectTrue (leq.isPrimed(), "primed after 5.5 s");
    }

    // ---------------------------------------------------------------------------
    section ("Silence gives the floor, not negative infinity");

    {
        ShortTermLeq leq;
        leq.prepare (1.0, fs, Weighting::z);

        for (int i = 0; i < (int) (2 * fs); ++i)
            leq.push (0.0);

        const double level = leq.currentLevelDb();

        expectTrue (std::isfinite (level), "silent input gives a finite level");
        expectNear (level, ShortTermLeq::silenceFloorDb, 1.0e-9, "silence reads the floor");
    }

    // ---------------------------------------------------------------------------
    section ("A-weighting is applied to the measurement");

    {
        // A 1 kHz tone is unaffected by A-weighting; a 100 Hz tone is 19.1 dB down.
        ShortTermLeq atKilohertz, atHundred;
        atKilohertz.prepare (2.0, fs, Weighting::a);
        atHundred.prepare (2.0, fs, Weighting::a);

        feedSine (atKilohertz, 1.0, 1000.0, 4.0, fs);
        feedSine (atHundred,   1.0,  100.0, 4.0, fs);

        const double difference = atKilohertz.currentLevelDb() - atHundred.currentLevelDb();
        std::printf ("  1 kHz minus 100 Hz through A-weighting: %.2f dB "
                     "(IEC 61672-1 says 19.1)\n", difference);

        expectNear (difference, 19.1, 0.15, "A-weighting attenuates 100 Hz by 19.1 dB");
    }

    // ---------------------------------------------------------------------------
    section ("The window length is reported after quantisation");

    {
        ShortTermLeq leq;
        leq.prepare (10.0, fs, Weighting::z);
        expectNear (leq.windowSeconds(), 10.0, 1.0e-9, "10 s window is exact");

        // 0.07 s is not a multiple of the 50 ms sub-block, so it rounds.
        leq.prepare (0.07, fs, Weighting::z);
        std::printf ("  asked for 0.07 s, got %.3f s\n", leq.windowSeconds());
        expectTrue (leq.windowSeconds() >= 0.05, "never shorter than one sub-block");
    }

    // ---------------------------------------------------------------------------
    section ("Long runs do not accumulate arithmetic drift");

    {
        // The running sum is maintained incrementally and rebuilt once per pass of the
        // ring. Check an hour of audio still reads correctly.
        ShortTermLeq leq;
        leq.prepare (1.0, fs, Weighting::z);

        feedSine (leq, 1.0, 1000.0, 600.0, fs);

        std::printf ("  after 10 minutes of continuous audio: %.6f dBFS\n",
                     leq.currentLevelDb());

        expectNear (leq.currentLevelDb(), -3.0103, 0.01, "still accurate after 10 minutes");
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
