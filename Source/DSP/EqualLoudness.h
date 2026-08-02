// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <vector>

/**
    ISO 226:2003 normal equal-loudness-level contours, and the compensation curve
    derived from them.

    This header deliberately depends on nothing but the C++ standard library. It is the
    one piece of Contourtonist whose numbers can be checked against a published table,
    so it must be testable without a plugin host, an audio device, or JUCE.

    ## What the standard gives us

    ISO 226:2003 §4.1 defines the sound pressure level `Lp` of a pure tone at frequency
    `f` which is perceived as having loudness level `Ln` (in phons):

        Lp = (10 / af) * lg(Af) - Lu + 94                                       (eq. 1)

        Af = 4.47e-3 * (10^(0.025*Ln) - 1.15)
           + (0.4 * 10^(((Tf + Lu) / 10) - 9))^af

    and §4.2 the inverse, `Ln` from `Lp`:

        Ln = 40 * lg(Bf) + 94                                                   (eq. 2)

        Bf = (0.4 * 10^((Lp + Lu)/10 - 9))^af
           - (0.4 * 10^((Tf + Lu)/10 - 9))^af
           + 0.005135

    `af` (the exponent for loudness perception), `Lu` (the linear transfer function
    normalised at 1 kHz) and `Tf` (the threshold of hearing) are tabulated at 29
    one-third-octave frequencies from 20 Hz to 12.5 kHz in the standard's Table 1.
    The values in EqualLoudness.cpp were transcribed from that table directly, not from
    a secondary source, and `Tables_matchPublishedContours` in the test suite checks the
    resulting contours against the numbers in Annex B.

    ## Validity, and why we let you leave it

    The standard states equation (1) applies from a lower limit of 20 phon up to:

        20 Hz to 4 000 Hz      90 phon
        5 000 Hz to 12 500 Hz  80 phon

    That is a problem for the job this plugin exists to do. A live system tuned to sit
    at 100 dB(A) and pulled back to 92 dB(A) by a noise limit has *both* of its
    endpoints above the validated ceiling, so a strictly conformant implementation
    computes no compensation at all at exactly the levels the tool is for.

    So the model has a policy (see @ref ExtrapolationPolicy). `Clamp` is strictly
    conformant and will quietly do nothing at show level. `Extend` keeps evaluating the
    equation outside the validated range — it stays smooth and finite well past 100
    phon, it just is not backed by listening data up there. `Extend` is the default
    because it is the useful answer, and every result carries @ref Contour::extrapolated
    so the GUI can say so out loud. Do not remove that flag to tidy the API up.

    ## Frequencies between the tabulated ones

    Table 1 gives coefficients at 29 points. For anything in between we interpolate the
    *derived curve* in log-frequency rather than interpolating `af`, `Lu` and `Tf`
    individually. The coefficients are fitted constants with no physical meaning of
    their own between tabulated points, whereas the contour is a real, smooth, measured
    thing. Interpolating the compensation curve is better still — being a difference of
    two contours of near-identical shape, it is smoother than either.
*/
namespace contourtonist::loudness
{

/** The 29 one-third-octave frequencies of ISO 226:2003 Table 1, in Hz. */
inline constexpr std::size_t numTabulated = 29;

/** Frequencies at which the standard tabulates af, Lu and Tf. */
const std::array<double, numTabulated>& tabulatedFrequencies();

/** What to do when asked for a contour outside the standard's validated range. */
enum class ExtrapolationPolicy
{
    /** Clamp the loudness level into the validated range. Strictly conformant; will
        produce no compensation between two levels that both sit above the ceiling. */
    clamp,

    /** Keep evaluating equation (1) outside the validated range. Smooth and finite,
        but unsupported by the standard's listening data. The default, because live
        sound operates above 90 phon and the alternative is a tool that does nothing. */
    extend
};

/** The lower limit of equation (1), in phons. Below this the standard is informative
    only, for want of data between 20 phon and the hearing threshold. */
inline constexpr double validMinPhon = 20.0;

/** The upper limit of equation (1) below 5 kHz, in phons. */
inline constexpr double validMaxPhonLF = 90.0;

/** The upper limit of equation (1) at and above 5 kHz, in phons. */
inline constexpr double validMaxPhonHF = 80.0;

/** The frequency at and above which the 80 phon ceiling applies, in Hz. */
inline constexpr double hfCeilingFrequency = 5000.0;

/** Sound pressure level, in dB, of a pure tone at @p frequencyHz which is perceived
    as equally loud to a 1 kHz tone at @p phon dB SPL.

    Evaluates ISO 226:2003 equation (1). @p frequencyHz is interpolated in log-frequency
    between the tabulated points and clamped to the tabulated range; @p phon is treated
    according to @p policy.

    By construction this returns @p phon exactly at 1 kHz, which is the definition of
    the phon and the sharpest available check that the implementation is correct.
*/
double soundPressureLevel (double frequencyHz, double phon,
                           ExtrapolationPolicy policy = ExtrapolationPolicy::extend);

/** Which inverse to use when going from sound pressure level back to phons.

    ISO 226:2003 prints equation (2) as the inverse of equation (1), but the two are
    not exact inverses of each other: the standard rounds several constants
    independently in each. The largest is `0.4`, which stands in for `10^-0.4 =
    0.398107` — worth 0.0206 dB on its own — with the `0.005135` and `94` constants
    of equation (2) accounting for the rest. Feeding equation (1)'s output straight
    back into equation (2) returns the loudness level high everywhere: 0.03 dB at
    1 kHz, up to about 0.056 dB where the `af` exponent is furthest from 0.25.

    Inverting equation (1) algebraically instead round-trips to machine precision.
    `tests/test_equal_loudness.cpp` measures both, so this is checked rather than
    asserted.

    0.03 dB is inaudible and far below the accuracy of any measurement feeding this
    plugin, so the choice does not matter acoustically. It matters architecturally:
    a reference level and a current level derived through inconsistent formulas put a
    small systematic bias into the compensation curve for no reason at all. So the
    default is the exact inverse.
*/
enum class InverseMethod
{
    /** Invert equation (1) algebraically. Round-trips exactly. The default. */
    exact,

    /** ISO 226:2003 equation (2) as printed. Normative, ~0.03 dB high on round trip. */
    standardEquation2
};

/** Loudness level in phons of a pure tone at @p frequencyHz and @p splDb.

    The inverse of @ref soundPressureLevel — see @ref InverseMethod for which inverse,
    and why there is a choice. Floors at the threshold of hearing, below which no
    loudness level exists.
*/
double loudnessLevel (double frequencyHz, double splDb,
                      ExtrapolationPolicy policy = ExtrapolationPolicy::extend,
                      InverseMethod method = InverseMethod::exact);

/** A compensation curve: the EQ needed to preserve perceived tonal balance when the
    reproduction level moves away from the level the material was balanced at. */
struct Contour
{
    /** Frequencies of @ref gainDb, in Hz — the 29 points of Table 1. */
    const std::array<double, numTabulated>* frequencies = nullptr;

    /** Gain to apply at each frequency, in dB. Always exactly 0 at 1 kHz. */
    std::array<double, numTabulated> gainDb {};

    /** The reference loudness level this curve was computed against, in phons. */
    double referencePhon = 0.0;

    /** The current loudness level this curve was computed against, in phons. */
    double currentPhon = 0.0;

    /** True when either endpoint fell outside the range ISO 226:2003 validates, and
        the curve therefore rests on extrapolation rather than on listening data.
        Surface this to the user; do not silently drop it. */
    bool extrapolated = false;

    /** Largest absolute gain in the curve, in dB. */
    double maxAbsGainDb() const;

    /** Gain at an arbitrary frequency, interpolated in log-frequency, in dB. */
    double gainAt (double frequencyHz) const;
};

/**
    The compensation curve between a reference level and a current level.

    Derivation. If material was balanced at @p referencePhon and is now reproduced at
    @p currentPhon, every frequency has been shifted by the same broadband amount
    `referencePhon - currentPhon`. But to stay *equally loud relative to 1 kHz*, the
    tone at `f` only needed to shift by the distance between the two contours there.
    The difference between those two is what we have to put back:

        G(f) = (referencePhon - currentPhon)
             - [ Lp(f, referencePhon) - Lp(f, currentPhon) ]

    At 1 kHz `Lp(1k, P) == P` by definition, so the bracket equals the first term and
    `G(1 kHz)` is identically zero. The curve therefore needs no normalisation and
    applies no broadband gain change — it only ever changes tonal balance, never
    loudness. That property is load-bearing: this plugin must not become a fader.

    Sign convention: reproducing *quieter* than the reference (`currentPhon <
    referencePhon`) yields positive gain at low frequencies, i.e. bass is restored.
    Reproducing louder yields negative low-frequency gain.
*/
Contour compensationCurve (double referencePhon, double currentPhon,
                           ExtrapolationPolicy policy = ExtrapolationPolicy::extend);

/** True if @p phon lies within the range ISO 226:2003 validates at @p frequencyHz. */
bool isWithinValidRange (double frequencyHz, double phon);

} // namespace contourtonist::loudness
