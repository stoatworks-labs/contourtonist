// SPDX-License-Identifier: MIT
#pragma once

#include "Biquad.h"
#include "EqualLoudness.h"

#include <array>
#include <cstddef>

/**
    Realises a compensation curve as a cascade of minimum-phase biquads.

    ## Why not linear phase

    Because this is a system EQ for live sound. A linear-phase realisation would match
    the target magnitude essentially perfectly, but it costs latency proportional to its
    low-frequency resolution, and getting a clean 8 dB of shape at 20 Hz needs a very
    long filter. Latency on a live output means lip sync errors on IMAG, comb filtering
    against nearby sources, and monitor-world timing problems. A minimum-phase cascade
    adds no latency at all and the phase response it does have is the same phase
    response any analogue or digital system EQ would contribute.

    So this bank adds **zero samples of latency**, and matching the target curve is a
    fitting problem rather than an exact one.

    ## How the fit works

    Fixed section frequencies, solved gains. Each of @ref numSections sections has a
    fixed centre frequency and Q; only its gain is fitted. Because the decibel response
    of a cascade is the *sum* of the sections' decibel responses, and a peaking filter's
    decibel response is very nearly proportional to its gain, the fit is a small linear
    least-squares problem:

        minimise  || A g - t ||^2 + lambda ||g||^2

    where `A[k][i]` is section i's response at grid frequency k for unit gain, `t` is
    the target curve, and lambda is a ridge term that keeps the solution from using
    large opposing gains in overlapping sections to chase a fraction of a dB.

    "Very nearly proportional" is not exactly proportional, so the solve is iterated
    against the true response of the built coefficients. Three passes is the default
    because the fit is fully converged by then — passes four through fifteen change the
    result in the fourth decimal place, which tests/test_filter_bank.cpp checks.

    ## How accurate it actually is

    Measured, not assumed. Worst-case deviation from the target over the fitting grid,
    across every curve the controller can request and every common sample rate:

        curve peak    worst error
          6 dB          0.19 dB
         12 dB          0.44 dB

    Error is roughly proportional to the size of the curve — about 4 % of it — and
    concentrates at 20 Hz and above 10 kHz, where the target is still climbing at the
    edge of the fitting range. It is comfortably below the uncertainty of the
    measurement driving the whole thing: a class 1 sound level meter is +/- 1.1 dB
    before anyone thinks about microphone placement.

    @ref BankCoefficients::worstErrorDb reports the figure for each fit, so the GUI can
    show it and nobody has to take this comment on trust.

    ## Threading

    @ref fit allocates nothing and takes microseconds, but it is not lock-free and has
    no business on the audio thread. Fit on the message thread, hand the resulting
    @ref BankCoefficients over, and let the audio thread only ever call
    @ref Processor::process. The controller rate-limits the curve, so consecutive fits
    differ by a tiny amount and the coefficient change is inaudible without any
    crossfading.
*/
namespace contourtonist::dsp
{

/** Number of second-order sections in the bank. */
inline constexpr std::size_t numSections = 16;

/** The fitted coefficients of the whole bank. Plain data — copy it across threads. */
struct BankCoefficients
{
    std::array<Coefficients, numSections> sections {};

    /** Gains the fit chose, in dB. Diagnostic only. */
    std::array<double, numSections> gainDb {};

    /** Worst absolute deviation from the target curve over the fitting grid, in dB.
        Check this: it is how you know the bank actually did what was asked. */
    double worstErrorDb = 0.0;

    /** Combined magnitude response of the whole bank at @p frequencyHz, in dB. */
    double magnitudeDb (double frequencyHz, double sampleRate) const;
};

/** Centre frequency of each section, in Hz. */
const std::array<double, numSections>& sectionFrequencies();

/**
    Fit the bank to @p target at @p sampleRate.

    @param target       the curve to match
    @param sampleRate   host sample rate
    @param iterations   refinement passes; 3 is the tested default
    @returns            coefficients, including the achieved worst-case error
*/
BankCoefficients fit (const loudness::Contour& target, double sampleRate,
                      int iterations = 3);

/** A bank of sections that has been given coefficients, for one or more channels. */
class Processor
{
public:
    /** Maximum channels supported. Stereo plus a bit of headroom for surround stems. */
    static constexpr std::size_t maxChannels = 16;

    /** Prepare for @p numChannels of audio. Clears all state. */
    void prepare (std::size_t numChannels);

    /** Install new coefficients. Real-time safe; does not touch filter state. */
    void setCoefficients (const BankCoefficients& c) noexcept;

    /** Clear filter state without changing coefficients. */
    void reset() noexcept;

    /** Process one sample on @p channel. */
    double processSample (std::size_t channel, double x) noexcept
    {
        auto& chain = chains[channel];
        for (std::size_t i = 0; i < numSections; ++i)
            x = chain[i].processSample (x);
        return x;
    }

    /** Process a block of @p numSamples in place on @p channel. */
    void processBlock (std::size_t channel, float* samples, std::size_t numSamples) noexcept;

private:
    std::array<std::array<Biquad, numSections>, maxChannels> chains {};
    std::size_t activeChannels = 0;
};

} // namespace contourtonist::dsp
