// SPDX-License-Identifier: MIT
//
// Integration test for the UDP level transport, over real sockets on the loopback
// interface. This is the link between the standalone that measures and the plugin
// instances that act on the measurement, so "it compiles" is not enough — a wire format
// mismatch or a receiver that never wakes up would leave every plugin instance sitting
// flat forever while looking perfectly healthy.
//
// Unlike the other test files this one links JUCE, because the transport is JUCE
// sockets. It is a separate target for that reason: the DSP tests stay dependency-free.

#include "../Source/Level/NetworkLevel.h"

#include <juce_core/juce_core.h>

#include <cstdio>

namespace
{
int failures = 0, checks = 0;

void expectTrue (bool c, const juce::String& what)
{
    ++checks;
    if (! c) { ++failures; std::printf ("  FAIL  %s\n", what.toRawUTF8()); }
}

void expectNear (double actual, double expected, double tol, const juce::String& what)
{
    ++checks;
    if (std::abs (actual - expected) > tol)
    {
        ++failures;
        std::printf ("  FAIL  %-46s got %9.4f want %9.4f\n",
                     what.toRawUTF8(), actual, expected);
    }
}

void section (const char* n) { std::printf ("\n%s\n", n); }

/** Wait until @p predicate is true or @p timeoutMs elapses. Returns whether it became
    true — polling rather than sleeping a fixed time, so the test is neither flaky on a
    loaded machine nor slower than it needs to be. */
template <typename Fn>
bool waitFor (Fn&& predicate, int timeoutMs = 3000)
{
    const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;

    while (juce::Time::currentTimeMillis() < deadline)
    {
        if (predicate())
            return true;

        juce::Thread::sleep (10);
    }

    return predicate();
}

/** A port unlikely to collide with anything, including a running Contourtonist. */
constexpr int testPort = 39877;

} // namespace

using namespace contourtonist;

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---------------------------------------------------------------------------
    section ("A receiver binds, and reports whether it did");

    net::LevelReceiver receiver;
    expectTrue (receiver.start (testPort), "receiver binds to its port");
    expectTrue (receiver.isListening(), "reports listening");
    expectTrue (receiver.getPort() == testPort, "reports the bound port");
    expectTrue (! receiver.latest().valid, "no reading before anything is sent");

    // ---------------------------------------------------------------------------
    section ("A published level arrives and parses");

    {
        net::LevelPublisher publisher;

        expectTrue (publisher.publish ("127.0.0.1", testPort, 88.0,
                                       meter::ReportedWeighting::c),
                    "publish reports success");

        expectTrue (waitFor ([&] { return receiver.latest().valid; }),
                    "reading arrives within the timeout");

        const auto snap = receiver.latest();
        expectNear (snap.splDb, 88.0, 0.01, "level survives the round trip");
        expectTrue (snap.weighting == meter::ReportedWeighting::c, "weighting survives");
        expectTrue (snap.isEquivalent, "published levels are tagged as Leq");
        expectTrue (snap.receivedAtMs > 0, "arrival is timestamped");
    }

    // ---------------------------------------------------------------------------
    section ("Levels keep arriving, and the newest wins");

    {
        net::LevelPublisher publisher;

        for (double level : { 90.0, 95.0, 99.5 })
        {
            publisher.publish ("127.0.0.1", testPort, level, meter::ReportedWeighting::a);
            expectTrue (waitFor ([&] {
                            return std::abs (receiver.latest().splDb - level) < 0.01;
                        }),
                        "receiver follows to " + juce::String (level));
        }

        expectNear (receiver.latest().splDb, 99.5, 0.01, "latest reading is the last sent");
        expectTrue (receiver.getPacketCount() >= 4, "packet count accumulates");
    }

    // ---------------------------------------------------------------------------
    section ("A negative level survives, because dBFS is negative");

    {
        net::LevelPublisher publisher;
        publisher.publish ("127.0.0.1", testPort, -23.5, meter::ReportedWeighting::z);

        expectTrue (waitFor ([&] { return receiver.latest().splDb < 0.0; }),
                    "negative level arrives");
        expectNear (receiver.latest().splDb, -23.5, 0.01, "negative level is exact");
    }

    // ---------------------------------------------------------------------------
    section ("Rubbish on the port is counted, not acted on");

    {
        const auto before = receiver.latest();

        juce::DatagramSocket raw { true };
        const char* junk = "this is not a level\n";
        raw.write ("127.0.0.1", testPort, junk, (int) std::strlen (junk));

        expectTrue (waitFor ([&] { return receiver.latest().malformedCount > 0; }),
                    "malformed datagram is counted");

        const auto after = receiver.latest();
        expectNear (after.splDb, before.splDb, 1e-9,
                    "the last good reading is not disturbed by rubbish");
    }

    // ---------------------------------------------------------------------------
    section ("A second receiver cannot steal the port");

    {
        net::LevelReceiver other;
        expectTrue (! other.start (testPort),
                    "binding an already-bound port fails rather than silently doing nothing");
    }

    // ---------------------------------------------------------------------------
    section ("Publishing to nowhere fails cleanly");

    {
        net::LevelPublisher publisher;
        expectTrue (! publisher.publish ("", testPort, 90.0, meter::ReportedWeighting::a),
                    "empty host refused");
        expectTrue (! publisher.publish ("127.0.0.1", 0, 90.0, meter::ReportedWeighting::a),
                    "port zero refused");
        expectTrue (! publisher.publish ("127.0.0.1", 99999, 90.0, meter::ReportedWeighting::a),
                    "out-of-range port refused");
    }

    // ---------------------------------------------------------------------------
    section ("Stopping releases the port");

    {
        receiver.stop();
        expectTrue (! receiver.isListening(), "reports not listening after stop");

        net::LevelReceiver reuse;
        expectTrue (reuse.start (testPort), "the port can be bound again afterwards");
        reuse.stop();
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
