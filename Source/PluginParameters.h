// SPDX-License-Identifier: MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Every parameter Contourtonist exposes, in one place.

    Note what is *not* a host-automatable parameter: the network port, the meter source
    selection, the calibration offset. Those are configuration, not performance
    controls — automating the UDP port from a DAW timeline is meaningless, and having
    the host recall a calibration offset taken on a different microphone would be
    actively dangerous. They live in the plugin state instead.
*/
namespace contourtonist::params
{

// Automatable parameter IDs.
inline constexpr const char* referenceSpl   = "referenceSpl";
inline constexpr const char* maxGain        = "maxGain";
inline constexpr const char* rate           = "rate";
inline constexpr const char* hysteresis     = "hysteresis";
inline constexpr const char* trackingRange  = "trackingRange";
inline constexpr const char* holdSeconds    = "holdSeconds";
inline constexpr const char* bypass         = "bypass";
inline constexpr const char* extrapolate    = "extrapolate";

// Non-automatable state keys.
inline constexpr const char* sourceMode      = "sourceMode";
inline constexpr const char* listenPort      = "listenPort";
inline constexpr const char* publishEnabled  = "publishEnabled";
inline constexpr const char* publishHost     = "publishHost";
inline constexpr const char* publishPort     = "publishPort";
inline constexpr const char* controlWeighting = "controlWeighting";
inline constexpr const char* calibrationOffset = "calibrationOffset";
inline constexpr const char* calibrationValid  = "calibrationValid";
inline constexpr const char* leqWindow       = "leqWindow";

/** Where the controlling level comes from. */
enum class SourceMode
{
    /** Listen on a UDP port for levels published by the standalone or by any script
        speaking the generic line protocol. The default for plugin instances. */
    network = 0,

    /** Measure this instance's own audio input. In the standalone with a measurement
        microphone on the input, this is the whole measurement chain. In a plugin it
        measures the programme rather than the room, which is a different and usually
        wrong thing — the GUI says so. */
    audioInput = 1
};

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace contourtonist::params
