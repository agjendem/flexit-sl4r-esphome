# research/ — source material and derivation

This directory holds two kinds of file: **copied source material from other
people's work**, and **our own derivation** built on top of it.

For the finished protocol specification in English, see
[`../PROTOCOL.md`](../PROTOCOL.md). The files here are the working notes behind
it.

## Ours

| File | Content |
|---|---|
| `protocol-notes.md` | The chronological derivation log, in Norwegian. Bus parameters, physical connection and pinout, telegram formats, the checksum algorithm (verified numerically in Python), every experiment, and every dead end. Written as a lab notebook, not a specification — see `../PROTOCOL.md` for the distilled result. |
| `captures/` | Raw bus captures from our own installation, with a parsing recipe. |

**Why keep the Norwegian notes?** They record how each conclusion was reached,
including the several readings that turned out to be wrong. That history is
worth more than a translation of the conclusions, which
[`../PROTOCOL.md`](../PROTOCOL.md) already gives in English. If you need
something specific from them and do not read Norwegian, open an issue and we
will translate that part.

## Copied source material

Everything below is **Copyright (c) 2018 Vongraven**, taken from
[Vongraven/Flexit-SL4R-master](https://github.com/Vongraven/Flexit-SL4R-master)
and reproduced unchanged under the MIT licence. The licence text is in
[`LICENSE-Vongraven`](LICENSE-Vongraven).

| File | Content |
|---|---|
| `Flexit_master.ino` | The original Arduino Mega implementation, tested against a real SL4R/CS50. The primary source of the protocol knowledge. |
| `vongraven-README.md` | His README, with byte dumps of a status line and a command telegram. Used to verify the checksum algorithm numerically. |
| `images/vongraven-topology.png` | Wiring diagram: MAX485 to the CS50 board's 4P4C port, in parallel with the CI50. The only wiring diagram that exists for this bus — copied here because the original repository has been untouched since December 2018 and could disappear. |

## Relationship to Vongraven's repository

This repository is **not a fork**. It shares no code with `Flexit_master.ino` —
the implementation here is an ESPHome external component in C++ with Python
codegen, built around a non-blocking state machine, targeting the ESP32. The
original is a standalone Arduino sketch for the Mega with blocking
`while (!Serial1.available())` loops.

What is inherited is the **protocol knowledge**: the synchronisation rule, the
byte offsets in the status telegram, the checksum algorithm and the command
telegram template. It has been reimplemented from scratch and verified
numerically against the examples in `vongraven-README.md` — not copied.

Several of the original readings turned out to be wrong for our unit, and that
is documented rather than quietly corrected: the sync rule was off by one byte,
`payload[5]` is two nibbles rather than a value to divide by 17, and the
poll/reply structure was not recognised at all. None of that diminishes the
original work — without it there would have been no starting point. In one
case the correction ran the other way: Vongraven's "0 = off, 128 = on" reading
of the afterheater bit was right all along, and it was our "correction" of it
that was wrong.
