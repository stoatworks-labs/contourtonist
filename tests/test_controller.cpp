// SPDX-License-Identifier: MIT
//
// Tests for the control loop, including a closed-loop simulation.
//
// The simulation is the point of this file. Contourtonist's whole risk is that it sits
// in a loop with the room: it changes the sound, the microphone hears the change, and
// the change feeds back into what it does next. Asserting in a comment that the loop is
// negative feedback is not evidence. Running it is.

#include "../Source/DSP/LoudnessController.h"

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
        std::printf ("  FAIL  %-52s got %9.4f  want %9.4f (tol %.3f)\n",
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

/**
    A crude but honest model of the acoustic loop.

    The room is at `trueLevelDb` before the plugin touches it. The plugin's curve adds
    energy at low frequency; some fraction of that shows up in the broadband level the
    microphone reports. `couplingAlpha` is that fraction — how much of the boosted
    region the weighted broadband measurement actually sees.

    Realistic values: A-weighting attenuates 20 Hz by about 50 dB, so almost none of a
    sub boost reaches an A-weighted reading and alpha is small, perhaps 0.05. Z-weighted
    on bass-heavy programme is the hostile case; 0.5 is pessimistic and 0.8 is
    absurd-but-let-us-see.
*/
struct RoomModel
{
    double trueLevelDb = 0.0;
    double couplingAlpha = 0.0;

    double measure (const contourtonist::loudness::Contour& curve) const
    {
        // Take the curve's low-frequency gain as the thing the measurement sees.
        const double lfGain = curve.gainAt (50.0);
        return trueLevelDb + couplingAlpha * lfGain;
    }
};

} // namespace

using namespace contourtonist;

int main()
{
    // ---------------------------------------------------------------------------
    section ("A controller with no measurement produces no EQ");

    {
        LoudnessController c;
        c.advance (0);
        expectTrue (c.status() == Status::waiting, "status is waiting");
        expectNear (c.currentCurve().maxAbsGainDb(), 0.0, 1.0e-9, "curve is flat");
    }

    // ---------------------------------------------------------------------------
    section ("At the reference level the curve stays flat");

    {
        LoudnessController c;
        Settings s;
        s.referenceSplDb = 100.0;
        c.setSettings (s);

        for (std::int64_t t = 0; t <= 60000; t += 100)
        {
            c.measurement (100.0, t);
            c.advance (t);
        }

        expectNear (c.currentCurve().maxAbsGainDb(), 0.0, 0.01,
                    "no compensation when the room is at reference");
    }

    // ---------------------------------------------------------------------------
    section ("A sustained level drop is compensated, slowly");

    {
        LoudnessController c;
        Settings s;
        s.referenceSplDb  = 100.0;
        s.rateDbPerSecond = 0.5;
        s.hysteresisDb    = 1.0;
        c.setSettings (s);

        // Room drops 15 dB and stays there.
        double after1s = 0.0, after10s = 0.0, after120s = 0.0;

        for (std::int64_t t = 0; t <= 120000; t += 100)
        {
            c.measurement (85.0, t);
            c.advance (t);

            if (t == 1000)   after1s   = c.currentCurve().maxAbsGainDb();
            if (t == 10000)  after10s  = c.currentCurve().maxAbsGainDb();
            if (t == 120000) after120s = c.currentCurve().maxAbsGainDb();
        }

        std::printf ("  curve peak after 1 s: %.2f dB, 10 s: %.2f dB, 120 s: %.2f dB\n",
                     after1s, after10s, after120s);

        expectTrue (after1s < 1.0, "the curve does not jump within the first second");
        expectTrue (after10s > after1s, "it is still moving at 10 s");
        expectTrue (after120s > 5.0, "it gets there eventually");

        // The rate limit is stated in dB of curve per second, so a second of movement
        // must not exceed it by much.
        expectTrue (after1s <= 0.5 * 1.0 + 0.15, "rate limit is honoured over 1 s");
    }

    // ---------------------------------------------------------------------------
    section ("Hysteresis stops the curve dithering on programme fluctuation");

    {
        LoudnessController c;
        Settings s;
        s.referenceSplDb = 100.0;
        s.hysteresisDb   = 1.0;
        c.setSettings (s);

        // Settle at 90 dB.
        for (std::int64_t t = 0; t <= 200000; t += 100)
        {
            c.measurement (90.0, t);
            c.advance (t);
        }

        const double settled = c.currentCurve().maxAbsGainDb();

        // Now wobble by +/- 0.8 dB, inside the deadband.
        double minGain = 1e9, maxGain = -1e9;

        for (std::int64_t t = 200000; t <= 260000; t += 100)
        {
            const double wobble = 0.8 * std::sin ((double) t * 0.01);
            c.measurement (90.0 + wobble, t);
            c.advance (t);

            minGain = std::min (minGain, c.currentCurve().maxAbsGainDb());
            maxGain = std::max (maxGain, c.currentCurve().maxAbsGainDb());
        }

        std::printf ("  settled %.3f dB, excursion under +/-0.8 dB wobble: %.4f dB\n",
                     settled, maxGain - minGain);

        expectTrue (maxGain - minGain < 0.05,
                    "sub-deadband fluctuation moves the curve by almost nothing");
    }

    // ---------------------------------------------------------------------------
    section ("The gain ceiling cannot be exceeded");

    {
        LoudnessController c;
        Settings s;
        s.referenceSplDb       = 100.0;
        s.maxGainDb            = 6.0;
        s.maxTrackingRangeDb   = 60.0;
        c.setSettings (s);

        for (std::int64_t t = 0; t <= 600000; t += 100)
        {
            c.measurement (40.0, t);   // absurdly quiet
            c.advance (t);
        }

        expectTrue (c.currentCurve().maxAbsGainDb() <= 6.0 + 1e-9,
                    "curve never exceeds maxGainDb");

        // And the shape must survive the limiting — still bass-heavy, still zero at 1k.
        expectTrue (c.currentCurve().gainAt (20.0) > c.currentCurve().gainAt (200.0),
                    "limiting preserves the shape of the curve");
        expectNear (c.currentCurve().gainAt (1000.0), 0.0, 1.0e-9,
                    "limiting preserves level neutrality at 1 kHz");
    }

    // ---------------------------------------------------------------------------
    section ("A silent or unplugged microphone cannot demand a huge boost");

    {
        LoudnessController c;
        Settings s;
        s.referenceSplDb     = 100.0;
        s.maxTrackingRangeDb = 20.0;
        s.maxGainDb          = 24.0;   // deliberately generous, so the range is what binds
        c.setSettings (s);

        for (std::int64_t t = 0; t <= 600000; t += 100)
        {
            c.measurement (-60.0, t);   // a dead input
            c.advance (t);
        }

        const double capped = loudness::compensationCurve (100.0, 80.0).maxAbsGainDb();
        std::printf ("  dead input settles at %.2f dB, 20 dB below reference gives %.2f dB\n",
                     c.currentCurve().maxAbsGainDb(), capped);

        expectNear (c.currentCurve().maxAbsGainDb(), capped, 0.1,
                    "tracking range, not the raw measurement, decides the curve");
    }

    // ---------------------------------------------------------------------------
    section ("Losing the measurement holds, then releases to flat");

    {
        LoudnessController c;
        Settings s;
        s.referenceSplDb = 100.0;
        s.holdSeconds    = 10.0;
        c.setSettings (s);

        for (std::int64_t t = 0; t <= 200000; t += 100)
        {
            c.measurement (88.0, t);
            c.advance (t);
        }

        const double settled = c.currentCurve().maxAbsGainDb();
        expectTrue (settled > 3.0, "compensating before the dropout");

        // Measurements stop. Advance only.
        c.advance (203000);
        expectTrue (c.status() == Status::stale, "goes stale after a few seconds");
        expectNear (c.currentCurve().maxAbsGainDb(), settled, 0.1,
                    "holds the curve while stale");

        for (std::int64_t t = 203000; t <= 400000; t += 100)
            c.advance (t);

        expectTrue (c.status() == Status::releasing, "eventually reports releasing");
        expectNear (c.currentCurve().maxAbsGainDb(), 0.0, 0.01,
                    "released all the way back to flat");
    }

    // ---------------------------------------------------------------------------
    section ("Closed-loop simulation: the microphone hears what the plugin did");

    {
        // This is the test that matters. For a range of acoustic coupling strengths,
        // close the loop and check the curve converges instead of running away or
        // ringing.
        for (const double alpha : { 0.0, 0.05, 0.2, 0.5, 0.8 })
        {
            LoudnessController c;
            Settings s;
            s.referenceSplDb  = 100.0;
            s.rateDbPerSecond = 0.5;
            s.hysteresisDb    = 0.0;   // no deadband, so any instability is visible
            s.maxGainDb       = 24.0;
            c.setSettings (s);

            RoomModel room { 88.0, alpha };

            std::vector<double> history;

            for (std::int64_t t = 0; t <= 600000; t += 100)
            {
                c.measurement (room.measure (c.currentCurve()), t);
                c.advance (t);
                history.push_back (c.currentCurve().gainAt (50.0));
            }

            // Converged?
            const double finalGain = history.back();
            double lateSwing = 0.0;

            for (std::size_t i = history.size() * 3 / 4; i < history.size(); ++i)
                lateSwing = std::max (lateSwing, std::abs (history[i] - finalGain));

            // Overshoot: did it ever go materially past where it ended up?
            double peak = 0.0;
            for (const double g : history)
                peak = std::max (peak, g);

            std::printf ("  alpha %.2f -> settles %.3f dB at 50 Hz, "
                         "late swing %.4f dB, overshoot %.4f dB\n",
                         alpha, finalGain, lateSwing, peak - finalGain);

            expectTrue (std::isfinite (finalGain), "loop stays finite at alpha " + std::to_string (alpha));
            expectTrue (lateSwing < 0.01,
                        "loop is settled, not oscillating, at alpha " + std::to_string (alpha));
            expectTrue (peak - finalGain < 0.05,
                        "loop does not overshoot at alpha " + std::to_string (alpha));
            expectTrue (finalGain < 12.0,
                        "loop does not run away at alpha " + std::to_string (alpha));

            // Negative feedback means stronger coupling gives *less* final boost, since
            // the boost partly satisfies its own demand.
            if (alpha == 0.0)
                expectTrue (finalGain > 5.0, "with no coupling the full curve is applied");
        }
    }

    // ---------------------------------------------------------------------------
    section ("Closed-loop offset shrinks with loop compensation");

    {
        auto settle = [] (LoopCompensation mode, double alpha)
        {
            LoudnessController c;
            Settings s;
            s.referenceSplDb    = 100.0;
            s.hysteresisDb      = 0.0;
            s.loopCompensation  = mode;
            s.assumedLoopGain   = 0.5 * alpha;   // rough: coupling times curve slope
            c.setSettings (s);

            RoomModel room { 88.0, alpha };

            for (std::int64_t t = 0; t <= 600000; t += 100)
            {
                c.measurement (room.measure (c.currentCurve()), t);
                c.advance (t);
            }

            return c.currentCurve().gainAt (50.0);
        };

        const double openLoopTarget = settle (LoopCompensation::none, 0.0);
        const double uncorrected    = settle (LoopCompensation::none, 0.5);
        const double corrected      = settle (LoopCompensation::estimated, 0.5);

        std::printf ("  target %.3f dB, uncorrected %.3f dB, corrected %.3f dB\n",
                     openLoopTarget, uncorrected, corrected);

        expectTrue (uncorrected < openLoopTarget,
                    "coupling makes the delivered curve fall short (negative feedback)");
        expectTrue (std::abs (corrected - openLoopTarget) < std::abs (uncorrected - openLoopTarget),
                    "loop compensation closes most of the gap");
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
