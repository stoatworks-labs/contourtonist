// SPDX-License-Identifier: MIT
//
// WebAssembly wrapper around Contourtonist's unmodified DSP core.
//
// This file plays the role PluginProcessor.cpp plays in the plugin: it owns a
// LoudnessController, refits the ContourFilterBank when the curve moves, and runs the
// bank over the audio buffers. The DSP classes are the plugin's own Source/DSP files,
// compiled verbatim — they depend on nothing but the C++ standard library, so unlike
// the Zero EQ web build no JUCE shim is needed at all.
//
// The demo has no measurement microphone. The page derives a simulated room level from
// an on-screen system fader (reference SPL + fader dB) and feeds it in through
// ctn_measurement exactly as the plugin's network level source would, so the
// controller's rate limit, hysteresis, ceiling and first-measurement behaviour are the
// real ones, not a re-enactment.

#include "DSP/ContourFilterBank.h"
#include "DSP/EqualLoudness.h"
#include "DSP/LoudnessController.h"

#include <emscripten/emscripten.h>

#include <cmath>
#include <cstdint>

using namespace contourtonist;

namespace
{

constexpr int kMaxBlock = 2048;

struct WebContourtonist
{
    LoudnessController controller;
    dsp::BankCoefficients coeffs;
    dsp::Processor processor;

    double sampleRate = 48000.0;

    // The (reference, current) pair the installed coefficients were fitted for, plus a
    // dirty flag for settings changes that alter the curve without moving either.
    double fitRef = -1.0, fitCur = -1.0;
    bool fitDirty = true;

    float bufL[kMaxBlock] = {}, bufR[kMaxBlock] = {};
};

WebContourtonist* W = nullptr;

void refitIfNeeded()
{
    const auto& curve = W->controller.currentCurve();

    if (! W->fitDirty && curve.referencePhon == W->fitRef && curve.currentPhon == W->fitCur)
        return;

    W->coeffs = dsp::fit (curve, W->sampleRate);
    W->processor.setCoefficients (W->coeffs);
    W->fitRef   = curve.referencePhon;
    W->fitCur   = curve.currentPhon;
    W->fitDirty = false;
}

} // namespace

extern "C"
{

EMSCRIPTEN_KEEPALIVE
void ctn_init (double sampleRate)
{
    delete W;
    W = new WebContourtonist();
    W->sampleRate = sampleRate;
    W->controller.reset();
    W->processor.prepare (2);
    refitIfNeeded();
}

EMSCRIPTEN_KEEPALIVE
void ctn_set_settings (double referenceSplDb, double rateDbPerSecond, double hysteresisDb,
                       double maxGainDb, double maxTrackingRangeDb, double holdSeconds,
                       int extrapolationPolicy)
{
    if (W == nullptr)
        return;

    Settings s;
    s.referenceSplDb     = referenceSplDb;
    s.rateDbPerSecond    = rateDbPerSecond;
    s.hysteresisDb       = hysteresisDb;
    s.maxGainDb          = maxGainDb;
    s.maxTrackingRangeDb = maxTrackingRangeDb;
    s.holdSeconds        = holdSeconds;
    s.extrapolation      = extrapolationPolicy == 0 ? loudness::ExtrapolationPolicy::clamp
                                                    : loudness::ExtrapolationPolicy::extend;
    W->controller.setSettings (s);
    W->fitDirty = true;
}

EMSCRIPTEN_KEEPALIVE
void ctn_reset_controller()
{
    if (W == nullptr)
        return;
    W->controller.reset();
    W->fitDirty = true;
    refitIfNeeded();
}

// Timestamps arrive as double milliseconds because cwrap has no int64 path; the
// controller only differences them, so the double's 2^53 integer range is ample.
EMSCRIPTEN_KEEPALIVE
void ctn_measurement (double splDb, double timestampMs)
{
    if (W != nullptr)
        W->controller.measurement (splDb, (std::int64_t) timestampMs);
}

EMSCRIPTEN_KEEPALIVE
void ctn_advance (double timestampMs)
{
    if (W == nullptr)
        return;
    W->controller.advance ((std::int64_t) timestampMs);
    refitIfNeeded();
}

// --- audio ---

EMSCRIPTEN_KEEPALIVE float* ctn_buf_l()    { return W->bufL; }
EMSCRIPTEN_KEEPALIVE float* ctn_buf_r()    { return W->bufR; }
EMSCRIPTEN_KEEPALIVE int    ctn_max_block() { return kMaxBlock; }

/** Run the fitted bank in place over both buffers. Always call it, even when the page
    is auditioning "compensation off" — keeping the filter state warm is what makes the
    A/B toggle click-free. The page chooses which buffer to output. */
EMSCRIPTEN_KEEPALIVE
void ctn_process (int numSamples)
{
    if (W == nullptr || numSamples <= 0 || numSamples > kMaxBlock)
        return;
    W->processor.processBlock (0, W->bufL, (std::size_t) numSamples);
    W->processor.processBlock (1, W->bufR, (std::size_t) numSamples);
}

EMSCRIPTEN_KEEPALIVE
void ctn_reset_audio()
{
    if (W != nullptr)
        W->processor.reset();
}

// --- readouts for the GUI ---

EMSCRIPTEN_KEEPALIVE int    ctn_status()             { return W != nullptr ? (int) W->controller.status() : 0; }
EMSCRIPTEN_KEEPALIVE double ctn_tracked_phon()       { return W != nullptr ? W->controller.trackedPhon() : 0.0; }
EMSCRIPTEN_KEEPALIVE double ctn_measured_spl()       { return W != nullptr ? W->controller.lastMeasuredSplDb() : 0.0; }
EMSCRIPTEN_KEEPALIVE int    ctn_extrapolated()       { return W != nullptr && W->controller.isExtrapolated() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE double ctn_curve_max_abs_db()   { return W != nullptr ? W->controller.currentCurve().maxAbsGainDb() : 0.0; }
EMSCRIPTEN_KEEPALIVE double ctn_fit_worst_error_db() { return W != nullptr ? W->coeffs.worstErrorDb : 0.0; }

/** The controller's target curve at @p frequencyHz, in dB. */
EMSCRIPTEN_KEEPALIVE
double ctn_target_db (double frequencyHz)
{
    return W != nullptr ? W->controller.currentCurve().gainAt (frequencyHz) : 0.0;
}

/** The fitted bank's actual magnitude response at @p frequencyHz, in dB. */
EMSCRIPTEN_KEEPALIVE
double ctn_bank_db (double frequencyHz)
{
    return W != nullptr ? W->coeffs.magnitudeDb (frequencyHz, W->sampleRate) : 0.0;
}

/** Pure curve query, bypassing the controller: the compensation between two levels.
    Used by the verification harness to compare against a native build. */
EMSCRIPTEN_KEEPALIVE
double ctn_curve_direct_db (double referencePhon, double currentPhon, double frequencyHz)
{
    return loudness::compensationCurve (referencePhon, currentPhon).gainAt (frequencyHz);
}

} // extern "C"
