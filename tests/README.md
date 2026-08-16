# tests/ — protocol tests

```bash
./tests/run.sh
```

Needs a C++17 compiler and `gunzip`. No ESPHome, no ESP32 toolchain, no
hardware — the point is that there is never a reason not to run them.

## What is tested

Everything here exercises [`../components/flexit_sl4r/protocol.h`](../components/flexit_sl4r/protocol.h),
which is the real code the firmware runs, not a reimplementation. That file was
extracted from the component precisely so it could be tested: it holds the
byte-level arithmetic and nothing else.

| Area | Why it is worth testing |
|---|---|
| **Checksum** | Fixed vectors read off the wire, never computed by this code. Also asserts the sum is order-dependent, so a plain byte sum cannot pass. |
| **Frame validation** | Including a mutation of the `TYPE` byte, which only fails if the checksum window starts in the right place. An off-by-one there reads fine and breaks every write — it did, for days. |
| **Fan level nibbles** | `0x31` (boost) must survive. The original reading divided by 17, which rejects it outright. |
| **Boost command** | The high nibble of `[15]` must be mirrored for all 16 values. Failing to mirror an unknown field is what switched the afterheater off three separate times. |
| **Parameter words** | Big-endian, and adjacent bytes are not necessarily one number — the "hour counter" that turned out to be two independent bytes. |
| **Recorded captures** | Both files in [`../research/captures/`](../research/captures/) are replayed through the frame scanner. |

## The capture replay

The scanner uses the same rule as the firmware: find `0xC3`, trust `LEN`, verify
the checksum, and on failure advance **one byte** rather than the claimed
length, because a `0xC3` inside a payload is perfectly normal.

The assertions are tied to what [`../PROTOCOL.md`](../PROTOCOL.md) claims the
captures contain, so a decoding change that contradicts the documentation fails
here rather than in the field:

- the 2026-08-13 capture must show a setpoint sweep reaching both 15 and 25 °C
  and at least one boost fan level (nibbles differing)
- every status telegram must carry a *valid* fan level
- over 80 % of the byte stream must be consumed as frames, which catches a
  resynchronisation bug that a checksum test alone would not

Current output: 4500 frames from 139 kB with 3 resynchronisation skips, and
1251 from 39 kB with 3.

## What is not tested

The parts that need hardware or time: enumeration, the transmit path, the
boost timer, and anything to do with how the CS 50 *responds*. Those are
verified against the live bus and written up in `PROTOCOL.md` with the
measurements behind them.

## A note on the first run

The checksum section originally carried a vector for our own idle reply whose
expected bytes were written from memory rather than read from a capture. The
suite failed on its first execution and the vector was replaced with two real
ones, taken from frames that occur 414 and 234 times in the 2026-08-13 capture.
That is a fair summary of what these tests are for.
