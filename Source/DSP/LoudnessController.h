// SPDX-License-Identifier: MIT
#pragma once

#include "EqualLoudness.h"

#include <cstdint>

/**
    Turns a stream of measured sound pressure levels into a compensation curve, slowly
    and predictably enough to be safe on a show.

    Like EqualLoudness.h this depends on nothing but the standard library, so the
    control behaviour can be simulated and tested without audio.

    ## The loop, and why it is stable

    In the intended deployment the plugin sits on a live output, the microphone hears
    the PA, and the measurement therefore includes the plugin's own effect on the sound.
    That is a closed loop, so its sign matters more than anything else in this file.

    It is negative feedback:

        more LF boost  ->  measured level rises
                       ->  (reference - current) shrinks
                       ->  less LF boost

    The compensation curve is a function of how far *below* the reference the room is.
    Anything the plugin does to raise the measured level reduces its own output. The
    loop is self-limiting; it converges rather than running away.

    Two consequences follow, and they are the real design constraints:

    1. **The equilibrium is offset.** The loop settles where the applied boost and the
       level it adds are consistent, which is slightly short of the boost the operator
       asked for. Loop gain is the product of two terms: how much curve gain a dB of
       level buys (about 0.5 dB at 20 Hz, falling to 0 at 1 kHz), and what fraction of
       the measured broadband level lives in the boosted region. The second is small
       under A-weighting, which attenuates 20 Hz by around 50 dB, and much larger under
       Z. In practice the product sits well below 1 and the offset is a fraction of a
       dB. @ref LoopCompensation can divide it out if you want the asked-for curve.

    2. **Oscillation, not runaway, is the failure mode.** A loop with delay and enough
       gain rings. The delay here is dominated by the integration time of whatever is
       measuring — 10 s of short-term LEQ is 10 s of lag. @ref Settings::rateDbPerSecond
       is the defence: hold the curve's rate of change far below the measurement
       bandwidth and the loop cannot ring, at the cost of responding slowly to a real
       level change. Slow is correct here. Nobody wants to hear the system EQ move.

    ## What it does when the measurement stops

    A meter unplugged mid-show, a network drop, an operator closing the analyser: the
    controller must not invent a level. It holds the last good value, reports
    @ref Status::stale, and after @ref Settings::holdSeconds ramps the curve back to
    flat at the normal rate limit. Flat is the safe default — it is the sound the system
    was tuned for.
*/
namespace contourtonist
{

/** Whether to correct for the closed-loop offset described above. */
enum class LoopCompensation
{
    /** Apply the curve as computed. The loop settles slightly short of it. */
    none,

    /** Divide out an estimate of the loop gain so the delivered curve matches the
        computed one. Relies on an assumed loop gain, so it trades a known small error
        for an estimated one. Off by default. */
    estimated
};

/** How the controller is currently behaving. */
enum class Status
{
    /** No measurement has arrived yet. Curve is flat. */
    waiting,

    /** Measurements are arriving and the curve is live. */
    tracking,

    /** Measurements have stopped. Holding the last curve. */
    stale,

    /** Measurements stopped long enough ago that the curve is being released to flat. */
    releasing,

    /** Bypassed by the user. */
    bypassed
};

/** Everything the operator can set. Defaults are for live sound reinforcement. */
struct Settings
{
    /** The level the system was tuned and the material balanced at, in dB SPL.
        Compensation is zero when the room is at this level. */
    double referenceSplDb = 100.0;

    /** Fastest the curve may move, in dB per second, measured at the frequency where
        it moves most. Deliberately slow: this is a system EQ on a show, and audible EQ
        movement is worse than slightly stale compensation. */
    double rateDbPerSecond = 0.5;

    /** Deadband, in dB. The target level must move by more than this before the curve
        follows, which stops the EQ dithering around on normal programme fluctuation. */
    double hysteresisDb = 1.0;

    /** Hard ceiling on the magnitude of any point in the curve, in dB. A bad
        calibration or a mic in the wrong place cannot produce more than this. */
    double maxGainDb = 12.0;

    /** Largest level *drop* below the reference that will be compensated, in dB.
        Beyond this the curve stops growing. Protects against the mic being unplugged
        into silence, or the PA being muted, being read as "the room is very quiet, so
        boost everything enormously". */
    double maxTrackingRangeDb = 30.0;

    /** How long to hold the last curve after measurements stop, in seconds, before
        releasing to flat. */
    double holdSeconds = 10.0;

    /** Whether to correct for closed-loop offset. */
    LoopCompensation loopCompensation = LoopCompensation::none;

    /** Assumed loop gain when @ref loopCompensation is estimated. Dimensionless. */
    double assumedLoopGain = 0.2;

    /** Extrapolation policy handed to the equal-loudness model. */
    loudness::ExtrapolationPolicy extrapolation = loudness::ExtrapolationPolicy::extend;

    /** User bypass. */
    bool bypassed = false;
};

/**
    The controller itself.

    Not real-time safe to configure, real-time safe to read. The audio thread calls
    @ref currentCurve; the message thread calls @ref measurement and @ref advance.
*/
class LoudnessController
{
public:
    LoudnessController() = default;

    /** Replace the settings. Takes effect on the next @ref advance. */
    void setSettings (const Settings& s) { settings = s; }

    /** The settings in force. */
    const Settings& getSettings() const { return settings; }

    /** Report a new measured level, in dB SPL, weighted however the source weights it.

        @param splDb        the measured level
        @param timestampMs  monotonic milliseconds, used only for staleness
    */
    void measurement (double splDb, std::int64_t timestampMs);

    /** Advance the controller to @p timestampMs, moving the curve toward its target no
        faster than the rate limit allows. Call this regularly — every GUI frame is
        ample — whether or not a measurement has arrived. */
    void advance (std::int64_t timestampMs);

    /** The curve to apply right now. */
    const loudness::Contour& currentCurve() const { return curve; }

    /** What the controller is doing. */
    Status status() const { return state; }

    /** The last measured level, in dB SPL, or 0 if none has arrived. */
    double lastMeasuredSplDb() const { return lastMeasurement; }

    /** The loudness level the curve is currently built for, in phons. This is the
        rate-limited, hysteresis-filtered value, not the raw measurement. */
    double trackedPhon() const { return tracked; }

    /** True when the curve rests on extrapolation beyond ISO 226:2003's validated
        range. Show this to the user. */
    bool isExtrapolated() const { return curve.extrapolated; }

    /** Discard all state, as if freshly constructed. */
    void reset();

private:
    void rebuildCurve();

    Settings settings;
    loudness::Contour curve = loudness::compensationCurve (100.0, 100.0);

    Status state = Status::waiting;

    double lastMeasurement = 0.0;   // dB SPL as reported
    double target          = 100.0; // phon the curve is heading toward
    double tracked         = 100.0; // phon the curve is built for right now
                                    // (both start at the default reference, where the
                                    //  curve is flat — see reset())

    bool haveMeasurement = false;

    std::int64_t lastMeasurementMs = 0;
    std::int64_t lastAdvanceMs     = 0;
};

} // namespace contourtonist
