# The control loop

Contourtonist sits across a live output and changes it based on a microphone listening to
that same output. Everything in `LoudnessController` exists because of that loop.

## The sign

The first question about any closed loop is its sign, and this one is **negative feedback**:

```
more LF boost  ->  measured level rises
               ->  (reference - current) shrinks
               ->  less LF boost
```

The compensation curve is a function of how far *below* the reference the room is, so
anything the plugin does to raise the measured level reduces its own output. The loop is
self-limiting. It converges rather than running away.

That is the good news, and it is worth stating clearly because the intuitive fear — mic
hears speaker, therefore runaway — points the wrong way here.

## Loop gain

Two terms multiply:

1. **How much curve gain a dB of level buys.** About 0.53 dB at 20 Hz, falling smoothly to
   zero at 1 kHz. It is the steepest slope of the compensation curve.
2. **What fraction of the measured broadband level lives in the boosted region.** Small
   under A-weighting, which attenuates 20 Hz by roughly 50 dB; much larger under Z on
   bass-heavy material.

The product sits well below 1 in any realistic case. `tests/test_controller.cpp` simulates
couplings from 0 to 0.8, which is far past pessimistic:

| coupling | settles at 50 Hz | late swing | overshoot |
|---|---|---|---|
| 0.00 | 5.029 dB | 0.0000 dB | 0.0000 dB |
| 0.05 | 4.926 dB | 0.0000 dB | 0.0005 dB |
| 0.20 | 4.641 dB | 0.0000 dB | 0.0012 dB |
| 0.50 | 4.159 dB | 0.0000 dB | 0.0013 dB |
| 0.80 | 3.768 dB | 0.0000 dB | 0.0033 dB |

No oscillation, essentially no overshoot, and the settled boost *falls* as coupling rises —
which is the signature of negative feedback and is the test that would catch a sign error.

## The two real consequences

### The equilibrium is offset

Because the boost partly satisfies its own demand, the loop settles slightly short of the
curve that was asked for: 5.03 dB open loop becomes 4.16 dB at a coupling of 0.5.

`LoopCompensation::estimated` divides out an assumed loop gain to land on the intended curve
(4.16 → 5.24 against a 5.03 target in the test). It is **off by default**, because it trades
a known small error for an estimated one, and the known error is a fraction of a dB.

### Ringing, not runaway, is the failure mode

A loop with delay and enough gain rings. The delay here is dominated by the integration time
of whatever is measuring — a 10 s short-term Leq is 10 s of lag.

`Settings::rateDbPerSecond` is the defence: hold the curve's rate of change far below the
measurement bandwidth and the loop cannot ring. The default is **0.5 dB/s**, which takes
about 16 seconds to deliver an 8 dB correction. Slow is correct. Nobody wants to hear the
system EQ move.

## The other guards

**Hysteresis** (1 dB default). The target only moves once the measurement leaves a deadband
around it, and then moves to the *edge* of the deadband rather than to the measurement, so a
level hovering on the boundary cannot make the curve dither. Measured effect of ±0.8 dB of
programme fluctuation on a settled curve: 0.0000 dB.

**Gain ceiling** (12 dB default). Applied last, so nothing upstream can defeat it. It
**scales** the curve rather than clipping individual points — a clipped curve is a different
and wronger curve, whereas a scaled one is simply less of the right one. Shape and level
neutrality both survive.

**Tracking range** (30 dB default). Caps how far below the reference the controller will
track. Without it, a muted PA or an unplugged microphone reads as a very quiet room and asks
for the largest boost available. This is the guard that matters most in practice, because
that failure is common and silent.

**Hold and release.** If measurements stop, the curve holds — the last reading is still the
best evidence about the room for a few seconds. After `holdSeconds` (10 s default) it
releases back to flat at the normal rate limit, because flat is the sound the system was
tuned for. The GUI reports `stale` and then `releasing` rather than pretending everything is
fine.

**No exception on the first measurement.** Tempting, since the first reading is new
information rather than a change. Wrong: a plugin instantiated mid-show into a room already
15 dB down would slam several dB of EQ in instantly. The rate limit is the one property that
makes this safe across a live system, and an exception is a hole in it. The cost is that the
first half-minute is under-compensated.

## Tuning it

| Situation | Change |
|---|---|
| Level changes are cues, not drift | Raise the rate; consider a shorter Leq window |
| Corporate speech, level moves slowly | Lower the rate, raise hysteresis |
| Compensation seems to fall short | Coupling; try `LoopCompensation::estimated` |
| Curve dithers | Raise hysteresis before lowering the rate |
| Curve slams on a mute | Lower the tracking range |
