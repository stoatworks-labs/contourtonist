// SPDX-License-Identifier: MIT
#pragma once

#include "MeterProtocol.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

/**
    Carrying a measured level from wherever it is measured to wherever it is needed.

    A plugin cannot open the measurement microphone itself: the host owns the audio
    device, and a plugin instance gets the channels the host hands it and nothing else.
    So the standalone owns the microphone and the calibration, and publishes the level;
    plugin instances subscribe. One microphone can drive every instance in the session,
    which is also the right answer for a live rig with several processing chains.

    The wire format is the generic line protocol from MeterProtocol.h — one reading per
    UDP datagram, as text. That is a deliberate choice over anything more efficient:
    a level arrives once or twice a second and is a dozen bytes, so there is nothing to
    optimise, and a text protocol means anyone with a meter Contourtonist does not
    support can drive it from a few lines of script. It also means `nc -u` is a working
    diagnostic tool.

    Unicast to a configured host and port, not broadcast. Broadcast on a show network
    that is also carrying audio is a bad neighbour, and multicast needs IGMP configured
    on switches nobody wants to touch at 6pm.
*/
namespace contourtonist::net
{

/** Receives levels published over UDP. */
class LevelReceiver : private juce::Thread
{
public:
    LevelReceiver();
    ~LevelReceiver() override;

    /** Start listening on @p port. Returns false if the port could not be bound —
        usually because another instance already has it, which on a machine running
        several plugin instances is the normal case and is why they should all be
        pointed at the same publisher rather than each binding their own port. */
    bool start (int port);

    void stop();

    bool isListening() const noexcept { return listening.load(); }

    int getPort() const noexcept { return boundPort.load(); }

    /** Most recent reading, and when it arrived. Thread safe. */
    struct Snapshot
    {
        double splDb = 0.0;
        meter::ReportedWeighting weighting = meter::ReportedWeighting::unknown;
        bool isEquivalent = false;
        juce::int64 receivedAtMs = 0;
        bool valid = false;

        /** Datagrams that arrived but could not be parsed. A steadily climbing number
            here means something is talking to the port that is not speaking the
            protocol, which the GUI should surface — silently discarding them is how
            you spend an afternoon wondering why the EQ is not moving. */
        int malformedCount = 0;
    };

    Snapshot latest() const;

    /** Total datagrams received, parsed or not. */
    int getPacketCount() const noexcept { return packetCount.load(); }

private:
    void run() override;

    std::unique_ptr<juce::DatagramSocket> socket;

    mutable juce::CriticalSection lock;
    Snapshot snapshot;

    std::atomic<bool> listening { false };
    std::atomic<int>  boundPort { 0 };
    std::atomic<int>  packetCount { 0 };
    std::atomic<int>  malformed { 0 };
};

/** Publishes levels over UDP, for the standalone to drive plugin instances. */
class LevelPublisher
{
public:
    /** Send @p splDb, tagged with @p weighting, to @p host : @p port.
        Returns false if the datagram could not be sent. */
    bool publish (const juce::String& host, int port, double splDb,
                  meter::ReportedWeighting weighting);

private:
    juce::DatagramSocket socket { true };
};

} // namespace contourtonist::net
