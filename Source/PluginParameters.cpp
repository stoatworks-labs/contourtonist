// SPDX-License-Identifier: MIT
#include "PluginParameters.h"

namespace contourtonist::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { referenceSpl, 1 }, "Reference level",
        NormalisableRange<float> (60.0f, 110.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB SPL")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { maxGain, 1 }, "Max gain",
        NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { rate, 1 }, "Rate",
        NormalisableRange<float> (0.05f, 6.0f, 0.01f, 0.5f), 0.5f,
        AudioParameterFloatAttributes()
            .withLabel ("dB/s")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " dB/s"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { hysteresis, 1 }, "Hysteresis",
        NormalisableRange<float> (0.0f, 6.0f, 0.1f), 1.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { trackingRange, 1 }, "Tracking range",
        NormalisableRange<float> (3.0f, 40.0f, 0.5f), 30.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { holdSeconds, 1 }, "Hold on signal loss",
        NormalisableRange<float> (1.0f, 120.0f, 1.0f, 0.5f), 10.0f,
        AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " s"; })));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { bypass, 1 }, "Bypass", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { extrapolate, 1 }, "Extend beyond ISO 226 range", true));

    return layout;
}

} // namespace contourtonist::params
