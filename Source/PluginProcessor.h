// SPDX-License-Identifier: MIT
#pragma once

#include "DSP/ContourFilterBank.h"
#include "DSP/LoudnessController.h"
#include "DSP/ShortTermLeq.h"
#include "Level/NetworkLevel.h"
#include "PluginParameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>

namespace contourtonist
{

/**
    The host-facing plugin.

    ## Threading

    Three threads touch this object and the split between them is the thing to get right:

    - **Audio thread** runs `processBlock`. It reads filter coefficients and writes
      audio, and does nothing else. No allocation, no locks, no fitting.
    - **Timer (message) thread** runs `updateControl`, thirty times a second. It reads
      the level source, advances the controller, refits the filter bank and publishes
      the new coefficients. All the expensive work is here, where it is allowed to be.
    - **Receiver thread** inside LevelReceiver takes datagrams off the socket.

    Coefficients cross from the timer thread to the audio thread through a double buffer
    and an atomic index. The audio thread never waits: it reads whichever buffer the
    index points at, and the timer thread writes the other one before flipping. Worst
    case the audio thread uses coefficients that are one update — 33 ms — stale, which
    against a curve rate-limited to 0.5 dB per second is 0.017 dB.
*/
class ContourtonistProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    ContourtonistProcessor();
    ~ContourtonistProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Contourtonist"; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }

    // --- Things the editor reads -----------------------------------------------------

    /** A consistent snapshot of everything the GUI shows, taken under one lock so the
        numbers on screen always describe the same moment. */
    struct DisplayState
    {
        double measuredSplDb = 0.0;
        double trackedPhon = 0.0;
        double referenceSplDb = 100.0;
        loudness::Contour curve;
        Status status = Status::waiting;
        bool extrapolated = false;
        double fitErrorDb = 0.0;
        bool haveLevel = false;
        bool listening = false;
        int listenPort = 0;
        int packetCount = 0;
        int malformedCount = 0;
        bool calibrated = false;
        bool leqPrimed = false;
        params::SourceMode sourceMode = params::SourceMode::network;
    };

    DisplayState getDisplayState() const;

    // --- Things the editor sets ------------------------------------------------------

    void setSourceMode (params::SourceMode mode);
    params::SourceMode getSourceMode() const { return sourceMode; }

    /** Bind the receiver to @p port. Returns false if the port is unavailable. */
    bool setListenPort (int port);
    int getListenPort() const { return listenPort; }

    void setPublishing (bool enabled, const juce::String& host, int port);
    bool isPublishing() const { return publishEnabled; }
    juce::String getPublishHost() const { return publishHost; }
    int getPublishPort() const { return publishPort; }

    void setControlWeighting (dsp::Weighting w);
    dsp::Weighting getControlWeighting() const { return controlWeighting; }

    void setLeqWindowSeconds (double seconds);
    double getLeqWindowSeconds() const { return leqWindowSeconds; }

    /** Set the dBFS-to-dB-SPL offset. Only meaningful for the audio-input source. */
    void setCalibrationOffset (double offsetDb, bool valid);
    double getCalibrationOffset() const { return calibrationOffsetDb; }
    bool hasCalibration() const { return calibrationValid; }

private:
    void timerCallback() override;
    void updateControl();
    void rebuildLeq();

    juce::AudioProcessorValueTreeState state;

    // --- Audio-thread-visible coefficient double buffer -------------------------------
    std::array<dsp::BankCoefficients, 2> coefficientBuffers;
    std::atomic<int> liveCoefficients { 0 };
    std::atomic<bool> coefficientsReady { false };

    dsp::Processor filterBank;

    // --- Control state, timer thread --------------------------------------------------
    LoudnessController controller;
    net::LevelReceiver receiver;
    net::LevelPublisher publisher;

    // Measurement of this instance's own input, for the standalone.
    dsp::ShortTermLeq inputLeq;
    std::atomic<double> inputLeqDb { 0.0 };
    std::atomic<bool> inputLeqPrimed { false };

    mutable std::mutex displayMutex;
    DisplayState display;

    double currentSampleRate = 48000.0;
    juce::int64 lastPublishMs = 0;
    juce::int64 lastNetworkPacketMs = 0;

    params::SourceMode sourceMode = params::SourceMode::network;
    int listenPort = 9878;
    bool publishEnabled = false;
    juce::String publishHost { "127.0.0.1" };
    int publishPort = 9878;
    dsp::Weighting controlWeighting = dsp::Weighting::c;
    double leqWindowSeconds = 5.0;
    double calibrationOffsetDb = 0.0;
    bool calibrationValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourtonistProcessor)
};

} // namespace contourtonist
