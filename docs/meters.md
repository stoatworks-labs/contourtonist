# Meter inputs — what works, and what has never met hardware

Contourtonist needs a level. This is where each way of getting one stands, stated plainly,
because the difference between "implemented" and "verified against the actual device" is the
whole difference between a tool you can take to a show and one you cannot.

## Summary

| Source | Implemented | Parser tested | Verified against the device |
|---|---|---|---|
| Generic UDP line protocol | Yes | Yes | Yes — over real sockets |
| This instance's audio input | Yes | Yes (synthetic signals) | **No** — no calibrator, no reference meter |
| Datalogger CSV | Parser only | Yes | **No** |
| NTi XL2 | Parser only | Yes | **No** — written from published docs |
| 10EaZy | **No** | — | — |

"Parser only" means the text handling is written and tested but no transport is wired to it
yet. The parsers are the part that would silently corrupt a reading; the transports are
straightforward and can be added when there is hardware to test them against.

## The generic UDP protocol

The one to use, and the one everything else should fall back to.

One reading per datagram, as text:

```
<level> [weighting] [leq|fast|slow]
```

All of these are valid:

```
95.3
95.3 A
95.3 A leq
95,3 C leq
```

A bare number is taken as an Leq at the weighting configured in the GUI. A decimal comma is
accepted. Anything that does not parse is counted and shown in the GUI rather than silently
dropped — a steadily climbing malformed count is how you discover something else is talking
to that port.

Default port 9878. Unicast, not broadcast: broadcast on a show network that is also carrying
audio is a bad neighbour, and multicast needs IGMP configured on switches nobody wants to
touch at 6pm.

**Why so simple.** A level arrives once or twice a second and is a dozen bytes. There is
nothing to optimise, and a text protocol means a meter Contourtonist has never heard of can
drive it from a few lines of script:

```bash
while true; do
  echo "$(my-meter --read-laeq)" | nc -u -w0 127.0.0.1 9878
  sleep 1
done
```

## NTi XL2

The XL2 has a documented remote-measurement command set over its USB serial interface, and
`parseXl2Response()` handles what it sends back — bare values (`95.3 dB`), labelled ones
(`LAEQ 95.3 dB`), and the dashed placeholder it returns between measurements.

That placeholder is the reason this parser exists rather than a `strtod` call. `--.- dB`
must read as "no value yet", not as zero. A level of zero fed into the controller reads as a
silent room and asks for maximum boost.

**Never tested against an XL2.** The command sequencing — initialising a measurement,
setting the integration time, polling — is written from the published documentation and has
not been run. Treat the transport as a starting point, not as working code.

## Datalogger CSV

`parseCsvRow()` handles a configurable column layout, comma or semicolon separators, quoted
fields and decimal commas.

The traps it exists for:

- **European exports** use semicolons *and* decimal commas together, precisely so the comma
  stays unambiguous. Point a comma-separated layout at one and the level column becomes the
  whole line.
- **A value containing both a comma and a point** is refused rather than guessed at. That is
  a thousands separator, and a level never needs one.
- **A header row that slips past `headerRows`** parses as nothing, not as a level.

The important limitation is not the parsing. It is that **a CSV is not a live source.** The
logging interval is whatever the meter was set to, often a second or more, and the file is
written after the interval closes. For a slow-moving compensation curve that is workable,
but it is the least responsive of the inputs and the latency is not under Contourtonist's
control.

## 10EaZy

**Not implemented.**

10EaZy is the meter that actually matters on regulated shows, which is exactly why it is not
implemented from guesswork. Its network protocol is not publicly documented, and inventing a
parser for a noise-compliance meter — where being wrong means either a fine or an
unnecessarily quiet show — is not a reasonable thing to do from inference.

Two honest routes forward:

1. **Use the generic protocol.** If the 10EaZy installation can be read by anything at all
   — its own logging, a screen-scrape, an operator typing a number — a few lines of script
   turn that into UDP datagrams Contourtonist understands.
2. **Get the protocol.** Either documentation from the vendor or a capture from a real
   installation. Then it is an afternoon's work, and the parser goes in `MeterProtocol.cpp`
   alongside the others with tests against real captured traffic.

## Calibration

The audio-input source produces dBFS until it is given the offset between dBFS and dB SPL
for that microphone, on that input, at that gain. That number can only come from a hardware
calibrator.

**Nothing in Contourtonist can check that offset is right.** A calibration is trusted
silently for the rest of the measurement, and a mistake is invisible afterwards because
everything downstream stays self-consistent and uniformly wrong.

Two consequences in the code:

- Without a calibration, levels are shown as dBFS and labelled as such, and the GUI warns
  that the reference level means nothing yet. It does not invent an SPL.
- **The calibration offset is deliberately not saved with the session.** It belongs to a
  microphone on an input at a gain setting, none of which travel in a session file.
  Restoring a stale one would produce confident, wrong readings. You recalibrate.

The calibration arithmetic has never had a calibrator connected to it.
