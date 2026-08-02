// SPDX-License-Identifier: MIT
#include "ShortTermLeq.h"

#include <algorithm>
#include <cmath>

namespace contourtonist::dsp
{

void ShortTermLeq::prepare (double windowSecondsWanted, double sampleRate,
                            Weighting weightingKind)
{
    rate = std::max (sampleRate, 1.0);
    weighting.prepare (weightingKind, rate);

    subBlockSamples = std::max<std::size_t> (1, (std::size_t) std::llround (subBlockSeconds * rate));

    const auto blocks = std::max<std::size_t> (
        1, (std::size_t) std::llround (std::max (windowSecondsWanted, subBlockSeconds) / subBlockSeconds));

    subBlockEnergy.assign (blocks, 0.0);

    reset();
}

void ShortTermLeq::reset()
{
    std::fill (subBlockEnergy.begin(), subBlockEnergy.end(), 0.0);
    writeIndex   = 0;
    filled       = 0;
    runningEnergy = 0.0;
    currentSum   = 0.0;
    currentCount = 0;
    primed       = false;
    weighting.reset();
}

double ShortTermLeq::windowSeconds() const noexcept
{
    return (double) subBlockEnergy.size() * subBlockSeconds;
}

void ShortTermLeq::closeSubBlock() noexcept
{
    const double meanSquare = currentCount > 0 ? currentSum / (double) currentCount : 0.0;

    // Maintain the window sum incrementally: subtract the sub-block being evicted,
    // add the new one. Recomputing the whole sum every time would be O(window) per
    // sub-block for no benefit.
    runningEnergy -= subBlockEnergy[writeIndex];
    runningEnergy += meanSquare;
    subBlockEnergy[writeIndex] = meanSquare;

    writeIndex = (writeIndex + 1) % subBlockEnergy.size();

    if (filled < subBlockEnergy.size())
    {
        ++filled;
        if (filled == subBlockEnergy.size())
            primed = true;
    }

    // The incremental sum accumulates rounding error over a long run. Every full pass
    // of the ring, rebuild it from the stored sub-blocks. A show is hours long and this
    // costs a few hundred additions twice a minute.
    if (writeIndex == 0)
    {
        double exact = 0.0;

        for (const double e : subBlockEnergy)
            exact += e;

        runningEnergy = exact;
    }

    currentSum   = 0.0;
    currentCount = 0;
}

void ShortTermLeq::push (double sample) noexcept
{
    const double weighted = weighting.processSample (sample);

    currentSum += weighted * weighted;

    if (++currentCount >= subBlockSamples)
        closeSubBlock();
}

void ShortTermLeq::pushBlock (const float* samples, std::size_t numSamples) noexcept
{
    for (std::size_t n = 0; n < numSamples; ++n)
        push ((double) samples[n]);
}

double ShortTermLeq::currentLevelDb() const noexcept
{
    const std::size_t count = std::max<std::size_t> (filled, 1);

    // Include the partially filled sub-block, so the reading responds within a sub-block
    // rather than stepping once every 50 ms.
    double energy = runningEnergy;
    double divisor = (double) count;

    if (currentCount > 0)
    {
        energy  += currentSum / (double) currentCount;
        divisor += 1.0;
    }

    const double meanSquare = energy / divisor;

    if (! (meanSquare > 0.0))
        return silenceFloorDb;

    // Full scale sine is 0 dBFS at an RMS of 1/sqrt(2); the reference for dBFS here is
    // a full-scale square, i.e. RMS 1.0 == 0 dBFS. That is the convention every meter
    // plugin uses and the one a calibration offset will have been derived against.
    const double db = 10.0 * std::log10 (meanSquare);

    return std::max (db + (calibrated ? calibrationOffsetDb : 0.0), silenceFloorDb);
}

} // namespace contourtonist::dsp
