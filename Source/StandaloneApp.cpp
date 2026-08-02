// SPDX-License-Identifier: MIT
//
// A custom standalone application, replacing JUCE's default one.
//
// ## Why this file exists
//
// JUCE's stock standalone wrapper opens an audio **input** device for any plugin whose
// processor has input channels — which is every effect, including this one:
//
//     auto audioInputRequired = (inChannels > 0);
//     ...
//     init (audioInputRequired, preferredDefaultDeviceName);
//
// Contourtonist does not need one. Its default level source is the network: the
// standalone listens on UDP, or publishes what a plugin instance should act on. An
// audio input is needed only when the operator explicitly picks "This instance's audio
// input" as the source, and at that point they can choose the device themselves in the
// standalone's own audio settings, where they can also pick the *right* one — a
// measurement microphone is rarely the system default.
//
// So opening one unasked was always wrong: it prompts for microphone permission the
// first time the app is launched, for a feature the operator has not asked for.
//
// ## The failure that made it urgent
//
// On a machine with several virtual audio devices installed — NDI, Dante-style bridges,
// conferencing drivers — asking CoreAudio for a default input *and* a default output
// makes JUCE build an `AudioIODeviceCombiner` across two different devices. That path
// can block indefinitely inside `AudioDeviceCreateIOProcID`, on a `mach_msg` to
// coreaudiod, and it does so on the **message thread, before the window is created**.
//
// The result is an application that launches, appears in the Dock, consumes no CPU,
// writes nothing to stderr, produces no crash report, and never shows a window. It is
// not obviously a hang; it looks like the app simply failing to start. Every JUCE
// effect standalone on such a machine behaves the same way, which is what made it
// clear this was environmental rather than specific to this plugin.
//
// Declining the input by default sidesteps the combiner entirely: one device, opened
// for output only, which is all the standalone needs to sit there measuring and
// publishing.
//
// ## What this does not do
//
// It does not stop the operator using an input. `AudioDeviceSetup::inputDeviceName`
// being empty is a *default*, not a constraint — the audio settings dialog offers every
// input on the system, and a choice made there is saved and reloaded on the next launch
// like any other. Nothing here removes a capability; it only stops one being taken
// without being asked for.

#include "PluginProcessor.h"

// These four, in this order, before the standalone header — it is written to be included
// by JUCE's own wrapper translation unit, which pulls them in first and does not include
// them itself. Without them the header fails to compile on its own base classes.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace contourtonist
{

class StandaloneApp final : public juce::JUCEApplication
{
public:
    StandaloneApp()
    {
        juce::PropertiesFile::Options options;

        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse;
            return;
        }

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel()
                .findColour (juce::ResizableWindow::backgroundColourId),
            createPluginHolder());

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    /** Write a first-run audio setup that opens an output but no input.

        Three routes into JUCE were tried before this one and none of them works:

        - `preferredSetupOptions` with an empty input name is overwritten by
          `AudioDeviceManager::insertDefaultDeviceNames`, which fills in the default
          input whenever `numInputChansNeeded > 0` — and it is 2, because the processor
          is a stereo effect.
        - Constraining `channelConfiguration` to `{0, 2}` does make
          `numInputChansNeeded` zero, but `getNumInputChannels()` then returns 0 for the
          audio settings dialog too, so the operator can never turn an input back on.
          That trades a hang for a permanently crippled app.
        - Overriding `StandalonePluginHolder::init` is not possible; it is not virtual.

        Saved device state is the one path that is honoured verbatim:
        `initialiseFromXML` takes the device names straight out of the XML and never
        calls `insertDefaultDeviceNames`. So an `audioInputDeviceName` of "" really does
        mean no input.

        This is a *default*, not a constraint. `getNumInputChannels()` still reports 2,
        the settings dialog still lists every input on the system, and once the operator
        picks one JUCE overwrites this file with their choice and it is restored on every
        later launch. Seeding only happens when there is no saved state at all.
    */
    void seedFirstRunAudioSetup()
    {
        auto* settings = appProperties.getUserSettings();

        if (settings == nullptr || settings->getXmlValue ("audioSetup") != nullptr)
            return;   // the operator has been here before; their choice wins

        // Enumerating device types scans for devices but opens none of them — no IOProc
        // is created, so this cannot hit the CoreAudio stall described at the top of
        // this file.
        juce::AudioDeviceManager probe;
        auto& types = probe.getAvailableDeviceTypes();

        if (types.isEmpty())
            return;

        auto* type = types.getFirst();
        type->scanForDevices();

        const auto outputs = type->getDeviceNames (false);
        const auto defaultOutput = outputs[type->getDefaultDeviceIndex (false)];

        if (defaultOutput.isEmpty())
            return;   // nothing sensible to write; let JUCE do whatever it would do

        juce::XmlElement setup ("DEVICESETUP");
        setup.setAttribute ("deviceType", type->getTypeName());
        setup.setAttribute ("audioOutputDeviceName", defaultOutput);
        setup.setAttribute ("audioInputDeviceName", "");

        settings->setValue ("audioSetup", &setup);
        settings->saveIfNeeded();
    }

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        seedFirstRunAudioSetup();

        return std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(),
            false,
            juce::String {},
            nullptr,
            juce::Array<juce::StandalonePluginHolder::PluginInOuts> {},
            false);   // don't auto-open MIDI devices; this plugin has no MIDI
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

} // namespace contourtonist

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new contourtonist::StandaloneApp();
}
