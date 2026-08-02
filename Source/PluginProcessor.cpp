// SPDX-License-Identifier: MIT
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace contourtonist
{
namespace
{
/** How often the control side runs. Fast enough that the curve moves smoothly on
    screen, slow enough that refitting sixteen biquads is free. */
constexpr int controlHz = 30;

/** How often the standalone publishes its level, in milliseconds. */
constexpr int publishIntervalMs = 250;
} // namespace

ContourtonistProcessor::ContourtonistProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Contourtonist", params::createLayout())
{
    receiver.start (listenPort);
    startTimerHz (controlHz);
}

ContourtonistProcessor::~ContourtonistProcessor()
{
    stopTimer();
    receiver.stop();
}

void ContourtonistProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    filterBank.prepare ((std::size_t) juce::jmax (1, getTotalNumOutputChannels()));
    filterBank.reset();

    rebuildLeq();
    controller.reset();

    // Start from a flat curve so nothing happens until a measurement justifies it.
    const auto flat = loudness::compensationCurve (100.0, 100.0);
    const auto fitted = dsp::fit (flat, currentSampleRate);

    coefficientBuffers[0] = fitted;
    coefficientBuffers[1] = fitted;
    liveCoefficients.store (0);
    coefficientsReady.store (true);
    filterBank.setCoefficients (fitted);
}

void ContourtonistProcessor::releaseResources()
{
    filterBank.reset();
    inputLeq.reset();
}

void ContourtonistProcessor::rebuildLeq()
{
    inputLeq.prepare (leqWindowSeconds, currentSampleRate, controlWeighting);

    if (calibrationValid)
        inputLeq.setCalibration (calibrationOffsetDb);
    else
        inputLeq.clearCalibration();
}

bool ContourtonistProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ContourtonistProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Measure the input before processing it. In the standalone with a measurement
    // microphone on the input, this is the room; in a plugin it is the programme, which
    // the GUI warns about. Either way it must be the *un*processed signal, or the
    // measurement includes our own EQ and the loop gains a term nobody intended.
    if (sourceMode == params::SourceMode::audioInput && numChannels > 0)
    {
        inputLeq.pushBlock (buffer.getReadPointer (0), (std::size_t) numSamples);
        inputLeqDb.store (inputLeq.currentLevelDb(), std::memory_order_relaxed);
        inputLeqPrimed.store (inputLeq.isPrimed(), std::memory_order_relaxed);
    }

    if (! coefficientsReady.load (std::memory_order_acquire))
        return;

    // Pick up whichever coefficient set the timer thread last published. No lock: the
    // timer thread writes the buffer the index does not point at, then flips.
    const auto index = liveCoefficients.load (std::memory_order_acquire);
    filterBank.setCoefficients (coefficientBuffers[(std::size_t) index]);

    for (int ch = 0; ch < numChannels; ++ch)
        filterBank.processBlock ((std::size_t) ch, buffer.getWritePointer (ch),
                                 (std::size_t) numSamples);
}

void ContourtonistProcessor::timerCallback()
{
    updateControl();
}

void ContourtonistProcessor::updateControl()
{
    const auto now = juce::Time::currentTimeMillis();

    // --- Settings from parameters ---------------------------------------------------
    Settings settings;
    settings.referenceSplDb     = (double) *state.getRawParameterValue (params::referenceSpl);
    settings.maxGainDb          = (double) *state.getRawParameterValue (params::maxGain);
    settings.rateDbPerSecond    = (double) *state.getRawParameterValue (params::rate);
    settings.hysteresisDb       = (double) *state.getRawParameterValue (params::hysteresis);
    settings.maxTrackingRangeDb = (double) *state.getRawParameterValue (params::trackingRange);
    settings.holdSeconds        = (double) *state.getRawParameterValue (params::holdSeconds);
    settings.bypassed           = *state.getRawParameterValue (params::bypass) > 0.5f;
    settings.extrapolation      = *state.getRawParameterValue (params::extrapolate) > 0.5f
                                ? loudness::ExtrapolationPolicy::extend
                                : loudness::ExtrapolationPolicy::clamp;

    controller.setSettings (settings);

    // --- Feed it a measurement ------------------------------------------------------
    bool haveLevel = false;
    double measured = 0.0;

    if (sourceMode == params::SourceMode::network)
    {
        const auto snap = receiver.latest();

        if (snap.valid && snap.receivedAtMs != lastNetworkPacketMs)
        {
            lastNetworkPacketMs = snap.receivedAtMs;
            measured = snap.splDb;
            haveLevel = true;
            controller.measurement (measured, snap.receivedAtMs);
        }
        else if (snap.valid)
        {
            measured = snap.splDb;
            haveLevel = true;
        }
    }
    else
    {
        // Only trust the integrator once its window has actually filled. Before that it
        // is an average over less time than asked for, which early in a show reads low
        // and would ask for a boost that is not warranted.
        if (inputLeqPrimed.load (std::memory_order_relaxed))
        {
            measured = inputLeqDb.load (std::memory_order_relaxed);
            haveLevel = true;
            controller.measurement (measured, now);
        }
    }

    controller.advance (now);

    // --- Refit and publish coefficients ---------------------------------------------
    const auto& curve = controller.currentCurve();
    const auto fitted = dsp::fit (curve, currentSampleRate);

    const int next = 1 - liveCoefficients.load (std::memory_order_relaxed);
    coefficientBuffers[(std::size_t) next] = fitted;
    liveCoefficients.store (next, std::memory_order_release);
    coefficientsReady.store (true, std::memory_order_release);

    // --- Publish our measurement, if asked ------------------------------------------
    if (publishEnabled && haveLevel && sourceMode == params::SourceMode::audioInput
        && now - lastPublishMs >= publishIntervalMs)
    {
        lastPublishMs = now;

        const auto reported = controlWeighting == dsp::Weighting::a ? meter::ReportedWeighting::a
                            : controlWeighting == dsp::Weighting::c ? meter::ReportedWeighting::c
                                                                    : meter::ReportedWeighting::z;
        publisher.publish (publishHost, publishPort, measured, reported);
    }

    // --- Snapshot for the GUI --------------------------------------------------------
    const auto snap = receiver.latest();

    DisplayState next_ {};
    next_.measuredSplDb   = measured;
    next_.trackedPhon     = controller.trackedPhon();
    next_.referenceSplDb  = settings.referenceSplDb;
    next_.curve           = curve;
    next_.status          = controller.status();
    next_.extrapolated    = controller.isExtrapolated();
    next_.fitErrorDb      = fitted.worstErrorDb;
    next_.haveLevel       = haveLevel;
    next_.listening       = receiver.isListening();
    next_.listenPort      = receiver.getPort();
    next_.packetCount     = receiver.getPacketCount();
    next_.malformedCount  = snap.malformedCount;
    next_.calibrated      = calibrationValid;
    next_.leqPrimed       = inputLeqPrimed.load (std::memory_order_relaxed);
    next_.sourceMode      = sourceMode;

    {
        const std::lock_guard<std::mutex> lock (displayMutex);
        display = next_;
    }
}

ContourtonistProcessor::DisplayState ContourtonistProcessor::getDisplayState() const
{
    const std::lock_guard<std::mutex> lock (displayMutex);
    return display;
}

void ContourtonistProcessor::setSourceMode (params::SourceMode mode)
{
    if (sourceMode == mode)
        return;

    sourceMode = mode;
    inputLeq.reset();
    inputLeqPrimed.store (false);
    controller.reset();
}

bool ContourtonistProcessor::setListenPort (int port)
{
    if (port <= 0 || port > 65535)
        return false;

    listenPort = port;
    return receiver.start (port);
}

void ContourtonistProcessor::setPublishing (bool enabled, const juce::String& host, int port)
{
    publishEnabled = enabled;
    publishHost = host;
    publishPort = port;
}

void ContourtonistProcessor::setControlWeighting (dsp::Weighting w)
{
    if (controlWeighting == w)
        return;

    controlWeighting = w;
    rebuildLeq();
    inputLeqPrimed.store (false);
}

void ContourtonistProcessor::setLeqWindowSeconds (double seconds)
{
    leqWindowSeconds = juce::jlimit (0.5, 60.0, seconds);
    rebuildLeq();
    inputLeqPrimed.store (false);
}

void ContourtonistProcessor::setCalibrationOffset (double offsetDb, bool valid)
{
    calibrationOffsetDb = offsetDb;
    calibrationValid = valid;

    if (valid)
        inputLeq.setCalibration (offsetDb);
    else
        inputLeq.clearCalibration();
}

void ContourtonistProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();

    // Configuration that is not an automatable parameter rides along in the same tree.
    auto config = tree.getOrCreateChildWithName ("config", nullptr);
    config.setProperty (params::sourceMode,      (int) sourceMode, nullptr);
    config.setProperty (params::listenPort,      listenPort, nullptr);
    config.setProperty (params::publishEnabled,  publishEnabled, nullptr);
    config.setProperty (params::publishHost,     publishHost, nullptr);
    config.setProperty (params::publishPort,     publishPort, nullptr);
    config.setProperty (params::controlWeighting, (int) controlWeighting, nullptr);
    config.setProperty (params::leqWindow,       leqWindowSeconds, nullptr);

    // Deliberately *not* saved: the calibration offset. It belongs to a microphone on
    // an input at a gain setting, none of which travel with the session file. Restoring
    // a stale one would silently produce confident, wrong SPL readings — see the note
    // in ShortTermLeq.h. The user recalibrates.

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void ContourtonistProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    state.replaceState (tree);

    if (auto config = tree.getChildWithName ("config"); config.isValid())
    {
        sourceMode       = (params::SourceMode) (int) config.getProperty (params::sourceMode, 0);
        publishEnabled   = config.getProperty (params::publishEnabled, false);
        publishHost      = config.getProperty (params::publishHost, "127.0.0.1").toString();
        publishPort      = config.getProperty (params::publishPort, 9878);
        controlWeighting = (dsp::Weighting) (int) config.getProperty (params::controlWeighting,
                                                                      (int) dsp::Weighting::c);
        leqWindowSeconds = config.getProperty (params::leqWindow, 5.0);

        const int port = config.getProperty (params::listenPort, 9878);
        setListenPort (port);
    }

    rebuildLeq();
    controller.reset();
}

juce::AudioProcessorEditor* ContourtonistProcessor::createEditor()
{
    return new ContourtonistEditor (*this);
}

} // namespace contourtonist

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new contourtonist::ContourtonistProcessor();
}
