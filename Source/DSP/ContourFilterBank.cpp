// SPDX-License-Identifier: MIT
#include "ContourFilterBank.h"

#include <algorithm>
#include <cmath>

namespace contourtonist::dsp
{
namespace
{

/** Section centre frequencies.

    Third-octave spacing across the bottom five octaves, where the compensation curve
    spends almost all of its range and has all of its detail, opening out to roughly
    octave spacing above 500 Hz where the curve is nearly a straight line through zero.

    Sixteen sections rather than twelve. Twelve fits the same curves to about 0.6 dB;
    sixteen gets to 0.44 dB, and most of the gain is at 20 Hz, which went from 0.43 dB
    short to 0.14 dB. Four more biquads per channel is nothing next to being visibly
    wrong at the one frequency the product exists to correct. */
constexpr std::array<double, numSections> kSectionFrequency {
     30.0,  150.0,
     20.0,   31.5,   50.0,   80.0,  125.0,  200.0,  315.0,  500.0,
    800.0, 1600.0, 3150.0, 6300.0, 12000.0,
   9000.0
};

/** Q of the peaking sections. A little under one octave of bandwidth. Broad enough
    that adjacent sections overlap and can reconstruct a smooth curve between them
    rather than leaving scallops; narrow enough that the solve is not degenerate. */
constexpr double kPeakQ = 1.0;

enum class Kind { lowShelf, peak, highShelf };

/** Two low shelves, not one.

    The compensation curve does not flatten out at the bottom — it keeps climbing all
    the way to 20 Hz, at very roughly a constant number of dB per octave. A single
    shelf is the wrong shape for that: below its corner frequency it levels off at its
    full gain, so it can match the slope or the endpoint but not both, and the fit
    gives up around half a dB short at 20 Hz. Two shelves at different corners stack
    into a staircase that tracks a continuing rise much better. */
constexpr std::array<Kind, numSections> kSectionKind {
    Kind::lowShelf, Kind::lowShelf,
    Kind::peak, Kind::peak, Kind::peak, Kind::peak, Kind::peak, Kind::peak,
    Kind::peak, Kind::peak, Kind::peak, Kind::peak, Kind::peak, Kind::peak,
    Kind::peak,
    Kind::highShelf
};

Coefficients buildSection (std::size_t i, double gainDb, double sampleRate)
{
    switch (kSectionKind[i])
    {
        case Kind::lowShelf:  return Coefficients::lowShelf  (kSectionFrequency[i], gainDb, sampleRate);
        case Kind::highShelf: return Coefficients::highShelf (kSectionFrequency[i], gainDb, sampleRate);
        case Kind::peak:
        default:              return Coefficients::peaking   (kSectionFrequency[i], kPeakQ, gainDb, sampleRate);
    }
}

/** Frequencies the fit is evaluated at: log-spaced across the audible band. */
constexpr std::size_t kGridPoints = 96;

std::array<double, kGridPoints> makeGrid (double sampleRate)
{
    std::array<double, kGridPoints> grid {};

    const double lo = std::log (20.0);
    const double hi = std::log (std::min (20000.0, sampleRate * 0.45));

    for (std::size_t k = 0; k < kGridPoints; ++k)
        grid[k] = std::exp (lo + (hi - lo) * (double) k / (double) (kGridPoints - 1));

    return grid;
}

/** Solve the n x n system M x = v in place by Gaussian elimination with partial
    pivoting. n is at most numSections, so this is a handful of microseconds and there
    is nothing to gain from anything cleverer. Returns false if M is singular. */
bool solveInPlace (std::array<std::array<double, numSections>, numSections>& m,
                   std::array<double, numSections>& v, std::size_t n)
{
    for (std::size_t col = 0; col < n; ++col)
    {
        std::size_t pivot = col;

        for (std::size_t r = col + 1; r < n; ++r)
            if (std::abs (m[r][col]) > std::abs (m[pivot][col]))
                pivot = r;

        if (std::abs (m[pivot][col]) < 1.0e-12)
            return false;

        if (pivot != col)
        {
            std::swap (m[pivot], m[col]);
            std::swap (v[pivot], v[col]);
        }

        for (std::size_t r = col + 1; r < n; ++r)
        {
            const double factor = m[r][col] / m[col][col];

            if (factor == 0.0)
                continue;

            for (std::size_t c = col; c < n; ++c)
                m[r][c] -= factor * m[col][c];

            v[r] -= factor * v[col];
        }
    }

    for (std::size_t i = n; i-- > 0;)
    {
        double sum = v[i];

        for (std::size_t c = i + 1; c < n; ++c)
            sum -= m[i][c] * v[c];

        v[i] = sum / m[i][i];
    }

    return true;
}

} // namespace

const std::array<double, numSections>& sectionFrequencies()
{
    return kSectionFrequency;
}

double BankCoefficients::magnitudeDb (double frequencyHz, double sampleRate) const
{
    double sum = 0.0;

    for (const auto& s : sections)
        sum += s.magnitudeDb (frequencyHz, sampleRate);

    return sum;
}

BankCoefficients fit (const loudness::Contour& target, double sampleRate, int iterations)
{
    BankCoefficients result;

    const auto grid = makeGrid (sampleRate);

    // Target curve sampled on the fitting grid.
    std::array<double, kGridPoints> want {};
    for (std::size_t k = 0; k < kGridPoints; ++k)
        want[k] = target.gainAt (grid[k]);

    // Basis: each section's response at a 1 dB probe gain. The response is not exactly
    // proportional to gain, which is why the solve below is iterated, but at 1 dB it is
    // close enough to be an excellent starting basis.
    constexpr double probeDb = 1.0;

    std::array<std::array<double, kGridPoints>, numSections> basis {};

    for (std::size_t i = 0; i < numSections; ++i)
    {
        const auto probe = buildSection (i, probeDb, sampleRate);

        for (std::size_t k = 0; k < kGridPoints; ++k)
            basis[i][k] = probe.magnitudeDb (grid[k], sampleRate) / probeDb;
    }

    // Normal equations for the basis, built once. Only the right-hand side changes
    // between refinement passes.
    //
    // The ridge term matters: neighbouring sections overlap heavily, so without it the
    // solver will happily use +30 dB and -29 dB in adjacent sections to shave a
    // hundredth of a dB off the fit. That fits the magnitude and wrecks the phase
    // response, the numerical conditioning and the headroom.
    constexpr double ridge = 1.0e-3;

    std::array<std::array<double, numSections>, numSections> normal {};

    for (std::size_t i = 0; i < numSections; ++i)
    {
        for (std::size_t j = 0; j < numSections; ++j)
        {
            double sum = 0.0;

            for (std::size_t k = 0; k < kGridPoints; ++k)
                sum += basis[i][k] * basis[j][k];

            normal[i][j] = sum + (i == j ? ridge * (double) kGridPoints : 0.0);
        }
    }

    std::array<double, numSections> gains {};

    for (int pass = 0; pass < std::max (1, iterations); ++pass)
    {
        // Residual between the target and what the current gains actually deliver.
        std::array<double, kGridPoints> residual {};

        for (std::size_t k = 0; k < kGridPoints; ++k)
        {
            double have = 0.0;

            for (std::size_t i = 0; i < numSections; ++i)
                have += buildSection (i, gains[i], sampleRate).magnitudeDb (grid[k], sampleRate);

            residual[k] = want[k] - have;
        }

        std::array<double, numSections> rhs {};

        for (std::size_t i = 0; i < numSections; ++i)
        {
            double sum = 0.0;

            for (std::size_t k = 0; k < kGridPoints; ++k)
                sum += basis[i][k] * residual[k];

            rhs[i] = sum;
        }

        auto m = normal;

        if (! solveInPlace (m, rhs, numSections))
            break;   // keep whatever we have; a singular system means the last pass stands

        for (std::size_t i = 0; i < numSections; ++i)
            gains[i] += rhs[i];
    }

    for (std::size_t i = 0; i < numSections; ++i)
    {
        result.gainDb[i]  = gains[i];
        result.sections[i] = buildSection (i, gains[i], sampleRate);
    }

    // Report the achieved accuracy rather than assuming it.
    result.worstErrorDb = 0.0;

    for (std::size_t k = 0; k < kGridPoints; ++k)
    {
        const double err = std::abs (result.magnitudeDb (grid[k], sampleRate) - want[k]);
        result.worstErrorDb = std::max (result.worstErrorDb, err);
    }

    return result;
}

void Processor::prepare (std::size_t numChannels)
{
    activeChannels = std::min (numChannels, maxChannels);
    reset();
}

void Processor::setCoefficients (const BankCoefficients& c) noexcept
{
    for (std::size_t ch = 0; ch < maxChannels; ++ch)
        for (std::size_t i = 0; i < numSections; ++i)
            chains[ch][i].setCoefficients (c.sections[i]);
}

void Processor::reset() noexcept
{
    for (auto& chain : chains)
        for (auto& section : chain)
            section.reset();
}

void Processor::processBlock (std::size_t channel, float* samples, std::size_t numSamples) noexcept
{
    if (channel >= activeChannels)
        return;

    for (std::size_t n = 0; n < numSamples; ++n)
        samples[n] = (float) processSample (channel, (double) samples[n]);
}

} // namespace contourtonist::dsp
