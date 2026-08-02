// SPDX-License-Identifier: MIT
#include "LoudnessController.h"

#include <algorithm>
#include <cmath>

namespace contourtonist
{

void LoudnessController::reset()
{
    state             = Status::waiting;
    lastMeasurement   = 0.0;
    // Both start at the reference, which is the level at which the curve is flat. A
    // zero here would mean "the room is 100 dB below reference" and ask for an enormous
    // curve on the first advance.
    target            = settings.referenceSplDb;
    tracked           = settings.referenceSplDb;
    haveMeasurement   = false;
    lastMeasurementMs = 0;
    lastAdvanceMs     = 0;
    curve             = loudness::compensationCurve (settings.referenceSplDb,
                                                     settings.referenceSplDb,
                                                     settings.extrapolation);
}

void LoudnessController::measurement (double splDb, std::int64_t timestampMs)
{
    if (! std::isfinite (splDb))
        return;

    lastMeasurement   = splDb;
    lastMeasurementMs = timestampMs;

    // The measured level is treated directly as a loudness level in phons. That is an
    // approximation — ISO 226 contours are pure tones in a free field, and programme
    // material is neither — but it is the approximation every loudness control has
    // made since Fletcher and Munson, and the alternative is a full loudness model.
    //
    // It does mean the weighting of the incoming measurement matters. A-weighting is
    // itself derived from the 40 phon contour, so feeding an A-weighted level in here
    // applies part of the equal-loudness correction twice. C or Z weighted is the
    // right input for this purpose even though shows are policed in dB(A); the
    // standalone measures both and sends the one configured for control.
    const double raw = splDb;

    // Never track further below the reference than the operator allows. Without this,
    // a muted PA or an unplugged microphone reads as a very quiet room and asks for
    // the largest boost the curve can produce.
    const double floorPhon = settings.referenceSplDb - settings.maxTrackingRangeDb;
    const double clamped   = std::clamp (raw, floorPhon, settings.referenceSplDb);

    // Hysteresis. The target only moves once the measurement has left a deadband
    // around it, and then it moves to the edge of that deadband rather than to the
    // measurement, so a level hovering on the boundary cannot make the curve dither.
    if (! haveMeasurement)
    {
        // First measurement sets the target only. The curve still has to slew there
        // from flat at the normal rate limit.
        //
        // It is tempting to snap instead, on the grounds that the first measurement is
        // new information rather than a change. Don't. A plugin instantiated mid-show
        // into a room already 15 dB down would slam several dB of EQ in instantly, and
        // the rate limit is the one property of this thing that makes it safe to put
        // across a live system. An exception to it is a hole in that guarantee, and
        // costs only that the first half-minute is under-compensated.
        target          = clamped;
        tracked         = settings.referenceSplDb;
        haveMeasurement = true;
        rebuildCurve();
    }
    else if (std::abs (clamped - target) > settings.hysteresisDb)
    {
        target = clamped > target ? clamped - settings.hysteresisDb
                                  : clamped + settings.hysteresisDb;
    }
}

void LoudnessController::advance (std::int64_t timestampMs)
{
    const double elapsedSeconds = lastAdvanceMs == 0
                                ? 0.0
                                : std::max (0.0, (double) (timestampMs - lastAdvanceMs) / 1000.0);
    lastAdvanceMs = timestampMs;

    if (settings.bypassed)
    {
        state = Status::bypassed;
        // Release to flat rather than snapping, so bypassing during a show is not an
        // event in itself.
        target = settings.referenceSplDb;
    }
    else if (! haveMeasurement)
    {
        state = Status::waiting;
        return;
    }
    else
    {
        const double sinceMeasurement = (double) (timestampMs - lastMeasurementMs) / 1000.0;

        if (sinceMeasurement > settings.holdSeconds)
        {
            // Measurements have been gone long enough that the last one is no longer
            // evidence about the room. Go back to flat, which is the sound the system
            // was tuned for, at the same rate limit as everything else.
            state  = Status::releasing;
            target = settings.referenceSplDb;
        }
        else if (sinceMeasurement > 2.0)
        {
            state = Status::stale;
        }
        else
        {
            state = Status::tracking;
        }
    }

    if (elapsedSeconds <= 0.0)
        return;

    // Rate limit. The limit is stated in dB of *curve* movement per second, but what
    // we can actually move is the tracked phon value, so convert: near the reference
    // the curve changes by roughly 0.53 dB at 20 Hz for every dB of level, and that is
    // the steepest point of the curve. Dividing by it turns the curve rate limit into
    // a phon rate limit that honours it at the worst frequency.
    constexpr double curveDbPerPhonDb = 0.53;

    const double maxPhonStep = (settings.rateDbPerSecond / curveDbPerPhonDb) * elapsedSeconds;
    const double error       = target - tracked;

    if (std::abs (error) <= maxPhonStep)
        tracked = target;
    else
        tracked += std::copysign (maxPhonStep, error);

    rebuildCurve();
}

void LoudnessController::rebuildCurve()
{
    double effectiveReference = settings.referenceSplDb;
    double effectiveCurrent   = tracked;

    if (settings.loopCompensation == LoopCompensation::estimated)
    {
        // The loop settles short of the computed curve by a factor of (1 - loopGain),
        // because the boost raises the level that produced it. Asking for a
        // proportionally larger deviation lands the delivered curve on the intended
        // one. Guarded well away from 1, where the correction would diverge — and a
        // loop gain anywhere near 1 would mean the loop itself is marginal.
        const double g = std::clamp (settings.assumedLoopGain, 0.0, 0.8);
        effectiveCurrent = effectiveReference - (effectiveReference - tracked) / (1.0 - g);
    }

    curve = loudness::compensationCurve (effectiveReference, effectiveCurrent,
                                         settings.extrapolation);

    // Hard ceiling, applied last so nothing upstream can defeat it. Scaling the whole
    // curve rather than clipping individual points keeps its shape — a clipped curve
    // is a different, wronger curve, whereas a scaled one is simply less of the right
    // one.
    const double peak = curve.maxAbsGainDb();

    if (peak > settings.maxGainDb && peak > 0.0)
    {
        const double scale = settings.maxGainDb / peak;

        for (auto& g : curve.gainDb)
            g *= scale;
    }
}

} // namespace contourtonist
