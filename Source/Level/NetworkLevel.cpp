// SPDX-License-Identifier: MIT
#include "NetworkLevel.h"

namespace contourtonist::net
{

LevelReceiver::LevelReceiver() : juce::Thread ("Contourtonist level receiver") {}

LevelReceiver::~LevelReceiver()
{
    stop();
}

bool LevelReceiver::start (int port)
{
    stop();

    socket = std::make_unique<juce::DatagramSocket> (true);

    if (! socket->bindToPort (port))
    {
        socket.reset();
        return false;
    }

    boundPort.store (port);
    listening.store (true);
    packetCount.store (0);
    malformed.store (0);

    {
        const juce::ScopedLock sl (lock);
        snapshot = {};
    }

    startThread (juce::Thread::Priority::normal);
    return true;
}

void LevelReceiver::stop()
{
    listening.store (false);

    if (socket != nullptr)
        socket->shutdown();

    stopThread (2000);
    socket.reset();
    boundPort.store (0);
}

void LevelReceiver::run()
{
    char buffer[512];

    while (! threadShouldExit())
    {
        if (socket == nullptr)
            break;

        // A timed wait rather than a blocking read, so that stop() does not depend on a
        // datagram arriving to unblock the thread.
        const int ready = socket->waitUntilReady (true, 200);

        if (ready < 0)
            break;

        if (ready == 0)
            continue;

        const int bytes = socket->read (buffer, (int) sizeof (buffer) - 1, false);

        if (bytes <= 0)
            continue;

        buffer[bytes] = 0;
        packetCount.fetch_add (1);

        // A datagram may carry more than one line if a publisher batches; take the
        // last complete one, since an older reading in the same packet is of no use.
        const juce::String text { juce::CharPointer_UTF8 (buffer) };
        auto lines = juce::StringArray::fromLines (text);

        std::optional<meter::Reading> parsed;

        for (int i = lines.size(); --i >= 0;)
        {
            const auto line = lines[i].toStdString();

            if (meter::trim (line).empty())
                continue;

            parsed = meter::parseGenericLine (line);
            break;
        }

        if (! parsed)
        {
            malformed.fetch_add (1);
            const juce::ScopedLock sl (lock);
            snapshot.malformedCount = malformed.load();
            continue;
        }

        const juce::ScopedLock sl (lock);
        snapshot.splDb          = parsed->splDb;
        snapshot.weighting      = parsed->weighting;
        snapshot.isEquivalent   = parsed->isEquivalent;
        snapshot.receivedAtMs   = juce::Time::currentTimeMillis();
        snapshot.valid          = true;
        snapshot.malformedCount = malformed.load();
    }
}

LevelReceiver::Snapshot LevelReceiver::latest() const
{
    const juce::ScopedLock sl (lock);
    return snapshot;
}

bool LevelPublisher::publish (const juce::String& host, int port, double splDb,
                              meter::ReportedWeighting weighting)
{
    if (host.isEmpty() || port <= 0 || port > 65535)
        return false;

    juce::String line;
    line << juce::String (splDb, 2) << " "
         << meter::weightingName (weighting) << " leq\n";

    const auto utf8 = line.toRawUTF8();
    const auto length = (int) std::strlen (utf8);

    return socket.write (host, port, utf8, length) == length;
}

} // namespace contourtonist::net
