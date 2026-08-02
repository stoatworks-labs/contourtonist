// SPDX-License-Identifier: MIT
#pragma once

#include "GUI/CurveDisplay.h"
#include "PluginProcessor.h"
#include "StoatworksAboutPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace contourtonist
{

/**
    The plugin window.

    Laid out around one question the operator actually has — "what is it doing to my
    system right now, and why" — so the curve and the three numbers behind it (measured
    level, reference, resulting phon) get the space, and the settings sit underneath.

    Everything that is uncertain is said out loud rather than hidden: whether a level is
    arriving at all, whether the curve rests on extrapolation past what ISO 226
    validates, whether the measurement is calibrated, and how well the filter bank
    actually fitted the curve. A system EQ that silently does something plausible is
    worse than one that says it is not sure.
*/
class ContourtonistEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit ContourtonistEditor (ContourtonistProcessor&);
    ~ContourtonistEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildControls();
    void applyNetworkSettings();
    static juce::String describe (Status);

    ContourtonistProcessor& processor;

    gui::CurveDisplay curve;

    juce::Label title, statusLine, measuredLabel, phonLabel, fitLabel, warningLabel;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<Knob, 6> knobs;

    /* Vendored from stoatworks-backend/about - see StoatworksAboutPanel.h.
       A child of the editor rather than a window of its own: a plugin must not
       put a second top-level window on a host's screen. */
    juce::TextButton aboutButton { "i" };
    stoatworks::AboutPanel aboutPanel;

    juce::ToggleButton bypassButton { "Bypass" };
    juce::ToggleButton extrapolateButton { "Extend past ISO 226" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> extrapolateAttachment;

    juce::ComboBox sourceBox, weightingBox;
    juce::TextEditor portField, publishHostField, publishPortField;
    juce::ToggleButton publishButton { "Publish level" };
    juce::Label sourceCaption, weightingCaption, portCaption, publishCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourtonistEditor)
};

} // namespace contourtonist
