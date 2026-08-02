// SPDX-License-Identifier: MIT
#include "PluginEditor.h"

namespace contourtonist
{
namespace
{
constexpr int windowWidth  = 720;
constexpr int windowHeight = 570;

/** Height of the header block, which paint() and resized() must agree on.

    The rows below add up to exactly this. They used to add up to more, and the overflow
    landed on the warning line — the one piece of text in the window that exists to be
    read, clipped through the descenders. Keep the arithmetic here rather than as two
    magic numbers in two functions. */
constexpr int titleRowHeight   = 26;
constexpr int measuredHeight   = 32;
constexpr int phonHeight       = 18;
constexpr int statusHeight     = 16;
constexpr int warningHeight    = 18;
constexpr int headerHeight     = titleRowHeight + measuredHeight + phonHeight
                               + statusHeight + warningHeight;

const juce::Colour background { 0xff0d0f12 };
const juce::Colour panel      { 0xff14161a };
const juce::Colour text       { 0xffd4d8de };
const juce::Colour dim        { 0xff8b929e };
const juce::Colour accent     { 0xff45b0e8 };
const juce::Colour warn       { 0xffe0a03a };

struct KnobSpec { const char* id; const char* name; };

constexpr KnobSpec knobSpecs[] {
    { params::referenceSpl,  "Reference" },
    { params::maxGain,       "Max gain" },
    { params::rate,          "Rate" },
    { params::hysteresis,    "Hysteresis" },
    { params::trackingRange, "Range" },
    { params::holdSeconds,   "Hold" }
};
} // namespace

ContourtonistEditor::ContourtonistEditor (ContourtonistProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    buildControls();
    setSize (windowWidth, windowHeight);
    startTimerHz (20);
}

ContourtonistEditor::~ContourtonistEditor()
{
    stopTimer();
}

void ContourtonistEditor::buildControls()
{
    auto styleLabel = [] (juce::Label& l, float size, juce::Colour colour,
                          juce::Justification just = juce::Justification::centredLeft)
    {
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, colour);
        l.setJustificationType (just);
    };

    addAndMakeVisible (title);
    title.setText ("Contourtonist", juce::dontSendNotification);
    styleLabel (title, 20.0f, text);

    addAndMakeVisible (statusLine);
    styleLabel (statusLine, 12.0f, dim);

    addAndMakeVisible (measuredLabel);
    styleLabel (measuredLabel, 26.0f, accent);

    addAndMakeVisible (phonLabel);
    styleLabel (phonLabel, 12.0f, dim);

    addAndMakeVisible (fitLabel);
    styleLabel (fitLabel, 11.0f, dim, juce::Justification::centredRight);

    addAndMakeVisible (warningLabel);
    styleLabel (warningLabel, 12.0f, warn);

    addAndMakeVisible (curve);

    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];

        addAndMakeVisible (k.slider);
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        k.slider.setColour (juce::Slider::textBoxTextColourId, text);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (k.label);
        k.label.setText (knobSpecs[i].name, juce::dontSendNotification);
        styleLabel (k.label, 11.0f, dim, juce::Justification::centred);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getState(), knobSpecs[i].id, k.slider);
    }

    addAndMakeVisible (bypassButton);
    bypassButton.setColour (juce::ToggleButton::textColourId, text);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getState(), params::bypass, bypassButton);

    addAndMakeVisible (extrapolateButton);
    extrapolateButton.setColour (juce::ToggleButton::textColourId, text);
    extrapolateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getState(), params::extrapolate, extrapolateButton);

    // --- Source ------------------------------------------------------------------------
    addAndMakeVisible (sourceCaption);
    sourceCaption.setText ("Level source", juce::dontSendNotification);
    styleLabel (sourceCaption, 11.0f, dim);

    addAndMakeVisible (sourceBox);
    sourceBox.addItem ("Network (UDP)", 1);
    sourceBox.addItem ("This instance's audio input", 2);
    sourceBox.setSelectedId (processor.getSourceMode() == params::SourceMode::network ? 1 : 2,
                             juce::dontSendNotification);
    sourceBox.onChange = [this]
    {
        processor.setSourceMode (sourceBox.getSelectedId() == 1 ? params::SourceMode::network
                                                                : params::SourceMode::audioInput);
    };

    addAndMakeVisible (weightingCaption);
    weightingCaption.setText ("Control weighting", juce::dontSendNotification);
    styleLabel (weightingCaption, 11.0f, dim);

    addAndMakeVisible (weightingBox);
    weightingBox.addItem ("C (recommended)", 1);
    weightingBox.addItem ("Z", 2);
    weightingBox.addItem ("A", 3);
    weightingBox.setSelectedId (processor.getControlWeighting() == dsp::Weighting::c ? 1
                              : processor.getControlWeighting() == dsp::Weighting::z ? 2 : 3,
                                juce::dontSendNotification);
    weightingBox.onChange = [this]
    {
        processor.setControlWeighting (weightingBox.getSelectedId() == 1 ? dsp::Weighting::c
                                     : weightingBox.getSelectedId() == 2 ? dsp::Weighting::z
                                                                         : dsp::Weighting::a);
    };

    addAndMakeVisible (portCaption);
    portCaption.setText ("Listen port", juce::dontSendNotification);
    styleLabel (portCaption, 11.0f, dim);

    addAndMakeVisible (portField);
    portField.setText (juce::String (processor.getListenPort()));
    portField.setInputRestrictions (5, "0123456789");
    portField.onReturnKey = [this] { applyNetworkSettings(); };
    portField.onFocusLost = [this] { applyNetworkSettings(); };

    addAndMakeVisible (publishCaption);
    publishCaption.setText ("Publish to", juce::dontSendNotification);
    styleLabel (publishCaption, 11.0f, dim);

    addAndMakeVisible (publishButton);
    publishButton.setColour (juce::ToggleButton::textColourId, text);
    publishButton.setToggleState (processor.isPublishing(), juce::dontSendNotification);
    publishButton.onClick = [this] { applyNetworkSettings(); };

    addAndMakeVisible (publishHostField);
    publishHostField.setText (processor.getPublishHost());
    publishHostField.onReturnKey = [this] { applyNetworkSettings(); };
    publishHostField.onFocusLost = [this] { applyNetworkSettings(); };

    addAndMakeVisible (publishPortField);
    publishPortField.setText (juce::String (processor.getPublishPort()));
    publishPortField.setInputRestrictions (5, "0123456789");
    publishPortField.onReturnKey = [this] { applyNetworkSettings(); };
    publishPortField.onFocusLost = [this] { applyNetworkSettings(); };

    for (auto* editor : { &portField, &publishHostField, &publishPortField })
    {
        editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1c1f25));
        editor->setColour (juce::TextEditor::textColourId, text);
        editor->setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e35));
    }

    for (auto* box : { &sourceBox, &weightingBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1c1f25));
        box->setColour (juce::ComboBox::textColourId, text);
        box->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2e35));
    }
}

void ContourtonistEditor::applyNetworkSettings()
{
    const int port = portField.getText().getIntValue();

    if (port != processor.getListenPort() && port > 0 && port <= 65535)
    {
        if (! processor.setListenPort (port))
            portField.setText (juce::String (processor.getListenPort()));
    }

    processor.setPublishing (publishButton.getToggleState(),
                             publishHostField.getText().trim(),
                             publishPortField.getText().getIntValue());
}

juce::String ContourtonistEditor::describe (Status s)
{
    switch (s)
    {
        case Status::waiting:   return "waiting for a level";
        case Status::tracking:  return "tracking";
        case Status::stale:     return "holding - no recent measurement";
        case Status::releasing: return "releasing to flat - measurement lost";
        case Status::bypassed:  return "bypassed";
        default:                return "unknown";
    }
}

void ContourtonistEditor::timerCallback()
{
    const auto d = processor.getDisplayState();

    curve.setCurve (d.curve, d.extrapolated);

    const bool networkSource = d.sourceMode == params::SourceMode::network;

    if (! d.haveLevel)
    {
        curve.setInactive (true, networkSource
                                     ? "No level arriving on UDP " + juce::String (d.listenPort)
                                     : "Waiting for the Leq window to fill");
        measuredLabel.setText ("--.- dB", juce::dontSendNotification);
    }
    else
    {
        curve.setInactive (false, {});
        measuredLabel.setText (juce::String (d.measuredSplDb, 1)
                               + (d.calibrated || networkSource ? " dB SPL" : " dBFS"),
                               juce::dontSendNotification);
    }

    phonLabel.setText ("reference " + juce::String (d.referenceSplDb, 1)
                       + " dB   ->   tracking " + juce::String (d.trackedPhon, 1)
                       + " phon   ->   " + juce::String (d.curve.maxAbsGainDb(), 2)
                       + " dB peak correction",
                       juce::dontSendNotification);

    juce::String status = describe (d.status);

    if (networkSource)
    {
        status << "   |   UDP " << d.listenPort << (d.listening ? " listening" : " NOT BOUND")
               << "   |   " << d.packetCount << " packets";

        if (d.malformedCount > 0)
            status << ", " << d.malformedCount << " malformed";
    }
    else
    {
        status << "   |   audio input";

        if (! d.calibrated)
            status << ", uncalibrated";
    }

    statusLine.setText (status, juce::dontSendNotification);

    fitLabel.setText ("filter fit " + juce::String (d.fitErrorDb, 2) + " dB",
                      juce::dontSendNotification);

    // Warnings, most important first. Only one is shown — a stack of yellow text is
    // read as decoration rather than as a warning.
    juce::String warning;

    if (networkSource && ! d.listening)
        warning = "Could not bind UDP port " + juce::String (d.listenPort)
                + " - another instance probably has it. Point them all at one publisher.";
    else if (! networkSource && ! d.calibrated)
        warning = "No calibration: levels are dBFS, not dB SPL, so the reference level "
                  "means nothing until you calibrate.";
    else if (d.malformedCount > 0)
        warning = juce::String (d.malformedCount)
                + " datagrams could not be parsed - something is talking to this port "
                  "that is not speaking the protocol.";
    else if (d.extrapolated && d.curve.maxAbsGainDb() > 0.05)
        // Only when the curve is actually doing something. The reference level alone can
        // sit above 90 phon and set the extrapolated flag while the room is at the
        // reference and the curve is dead flat — warning that a flat curve rests on
        // extrapolation is true in the narrowest sense and misleading in every useful
        // one, and a warning that is usually noise stops being read.
        warning = "Levels are outside the range ISO 226:2003 validates (20-90 phon). "
                  "The curve is extrapolated.";

    warningLabel.setText (warning, juce::dontSendNotification);

    // Fields the user is not typing into should follow the processor.
    if (! portField.hasKeyboardFocus (false))
        portField.setText (juce::String (d.listenPort), juce::dontSendNotification);
}

void ContourtonistEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto bounds = getLocalBounds().reduced (12);
    bounds.removeFromTop (headerHeight + 8);

    g.setColour (panel);
    g.fillRoundedRectangle (bounds.removeFromBottom (208).toFloat(), 5.0f);
}

void ContourtonistEditor::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    // --- Header ------------------------------------------------------------------------
    auto header = bounds.removeFromTop (headerHeight);

    auto titleRow = header.removeFromTop (titleRowHeight);
    title.setBounds (titleRow.removeFromLeft (200));
    fitLabel.setBounds (titleRow.removeFromRight (140));

    measuredLabel.setBounds (header.removeFromTop (measuredHeight));
    phonLabel.setBounds (header.removeFromTop (phonHeight));
    statusLine.setBounds (header.removeFromTop (statusHeight));
    warningLabel.setBounds (header.removeFromTop (warningHeight));

    bounds.removeFromTop (8);

    // --- Settings panel at the bottom ---------------------------------------------------
    auto settings = bounds.removeFromBottom (208).reduced (10);

    auto knobRow = settings.removeFromTop (96);
    const int knobWidth = knobRow.getWidth() / (int) knobs.size();

    for (auto& k : knobs)
    {
        auto cell = knobRow.removeFromLeft (knobWidth);
        k.label.setBounds (cell.removeFromTop (14));
        k.slider.setBounds (cell.reduced (4, 0));
    }

    settings.removeFromTop (6);

    auto row = settings.removeFromTop (40);
    auto sourceCell = row.removeFromLeft (row.getWidth() / 2).reduced (0, 2);
    sourceCaption.setBounds (sourceCell.removeFromTop (14));
    sourceBox.setBounds (sourceCell);

    auto weightingCell = row.reduced (6, 2);
    weightingCaption.setBounds (weightingCell.removeFromTop (14));
    weightingBox.setBounds (weightingCell);

    settings.removeFromTop (6);

    auto netRow = settings.removeFromTop (40);
    auto portCell = netRow.removeFromLeft (110).reduced (0, 2);
    portCaption.setBounds (portCell.removeFromTop (14));
    portField.setBounds (portCell);

    netRow.removeFromLeft (10);

    auto publishCell = netRow.removeFromLeft (230).reduced (0, 2);
    publishCaption.setBounds (publishCell.removeFromTop (14));
    auto publishFields = publishCell;
    publishHostField.setBounds (publishFields.removeFromLeft (140));
    publishFields.removeFromLeft (6);
    publishPortField.setBounds (publishFields);

    netRow.removeFromLeft (10);
    publishButton.setBounds (netRow.removeFromLeft (130).withTrimmedTop (14));

    auto toggles = settings.removeFromTop (24);
    bypassButton.setBounds (toggles.removeFromLeft (100));
    extrapolateButton.setBounds (toggles.removeFromLeft (200));

    // --- Curve fills what is left --------------------------------------------------------
    curve.setBounds (bounds.reduced (0, 4));
}

} // namespace contourtonist
