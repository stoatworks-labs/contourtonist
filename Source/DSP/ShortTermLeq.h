// SPDX-License-Identifier: MIT
#pragma once

#include "Weighting.h"

#include <cstddef>
#include <vector>

/**
    Sliding short-term equivalent continuous sound level.

        Leq,T = 10 lg( (1/T) integral over T of p^2(t) / p0^2 dt )

    A true sliding window, not an exponential average. The distinction matters: an
    exponential average with a 10 s time constant still carries audible weight from
    events a minute old, whereas Leq over a 10 s window is exactly the last 10 seconds
    and nothing else. Contourtonist's whole behaviour is "match the level of the last
    few seconds", so the window has to mean what it says.

    The window is built from fixed sub-blocks rather than a per-sample ring, so cost is
    O(1) per sample and memory is a few hundred doubles rather than seconds of audio.
    The price is that the window quantises to the sub-block length — 50 ms by default,
    which is nothing against a window measured in seconds.

    ## Calibration

    Levels come out in dB SPL, which requires the offset between dBFS and dB SPL for
    this microphone on this input at this gain. That number can only come from a
    hardware calibrator, and nothing here can check it is right. @ref setCalibration
    takes it; without one, @ref currentLevelDb returns dBFS and @ref isCalibrated is
    false, and the GUI must say so rather than showing an invented SPL.
*/
namespace contourtonist::dsp
{

class ShortTermLeq
{
public:
    /** Default sub-block length. The window quantises to a multiple of this. */
    static constexpr double subBlockSeconds = 0.05;

    /** Prepare for @p windowSeconds of integration at @p sampleRate with @p weighting.
        Clears all accumulated energy. */
    void prepare (double windowSeconds, double sampleRate, Weighting weighting);

    /** Discard accumulated energy but keep the configuration. */
    void reset();

    /** Feed one sample. */
    void push (double sample) noexcept;

    /** Feed a block. */
    void pushBlock (const float* samples, std::size_t numSamples) noexcept;

    /** The level over the window: dB SPL if calibrated, dBFS if not.

        Returns @ref silenceFloorDb rather than negative infinity for a silent input, so
        that a muted PA produces a very low number instead of something no arithmetic
        downstream can cope with. */
    double currentLevelDb() const noexcept;

    /** True once the window has filled. Before that @ref currentLevelDb is an average
        over less than the requested time and should not be trusted or displayed as a
        measurement. */
    bool isPrimed() const noexcept { return primed; }

    /** Set the dBFS-to-dB-SPL offset from a hardware calibration. */
    void setCalibration (double offsetDb) noexcept
    {
        calibrationOffsetDb = offsetDb;
        calibrated = true;
    }

    void clearCalibration() noexcept { calibrated = false; calibrationOffsetDb = 0.0; }

    bool isCalibrated() const noexcept { return calibrated; }

    double getCalibrationOffsetDb() const noexcept { return calibrationOffsetDb; }

    /** The window actually in use, in seconds, after quantisation to sub-blocks. */
    double windowSeconds() const noexcept;

    /** Level reported for digital silence. */
    static constexpr double silenceFloorDb = -200.0;

private:
    void closeSubBlock() noexcept;

    WeightingFilter weighting;

    std::vector<double> subBlockEnergy;   // mean-square per closed sub-block
    std::size_t writeIndex = 0;
    std::size_t filled = 0;

    double runningEnergy = 0.0;           // sum over the ring, kept incrementally
    double currentSum = 0.0;              // energy accumulating in the open sub-block
    std::size_t currentCount = 0;
    std::size_t subBlockSamples = 1;

    double rate = 48000.0;
    double calibrationOffsetDb = 0.0;
    bool calibrated = false;
    bool primed = false;
};

} // namespace contourtonist::dsp
