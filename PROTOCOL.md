# The Flexit CS50 / CI50 RS485 protocol

This is a specification of the bus protocol used between a Flexit CS50 control
board and its CI50 control panel, reverse-engineered from a live SL4 R unit.

It is written as a reference: what the frames look like, what each field means,
and how confident we are about each claim. The chronological derivation — with
every dead end, and there were many — lives in
[`research/protocol-notes.md`](research/protocol-notes.md), in Norwegian.

Everything here was measured on **controller firmware `R1A 2.8`, panel `R1A 1.2`**.
Other firmware revisions may differ; please report if they do.

**Confidence is marked throughout:**

| Mark | Meaning |
|---|---|
| ✅ | Measured and verified, usually both directions or against an independent source |
| 🟡 | Consistent with all data we have, but not independently confirmed |
| ❓ | Hypothesis. Stated so it can be disproved |

---

## 1. Physical layer

| Property | Value | Confidence |
|---|---|---|
| Signalling | RS485, half duplex, 2-wire | ✅ |
| Baud rate | 19200, 8 data bits, no parity, 1 stop bit | ✅ |
| Topology | Multi-drop. The CI50's two 4P4C sockets are the **same** bus segment | ✅ |
| Connector | 4P4C ("RJ9/RJ22"), pin 1 = GND, 2 = B, 3 = A, 4 = +V | ✅ |
| Bus supply | ~11.8 V measured at the end of a 12 m panel cable | ✅ |
| Termination | Not needed at 19200 baud over household distances | 🟡 |

A node can be attached at the panel's spare socket, in parallel with the one in
use. There is no need to open the air handling unit itself.

**Do not expect auto-direction transceivers to be the problem.** A common
suspicion when transmission appears not to work is the DE/RE line. On an
M5Stack ATOM Tail485 there is no DE/RE signal at all, and transmission works
fine. If your writes are ignored, read section 3 first — the cause is almost
certainly protocol, not electronics.

---

## 2. Frame format

Two distinct message shapes travel on the bus. Confusing them for one framing
scheme with an 8-byte header is the single biggest trap in this protocol, and
it cost this project several days.

### 2.1 Poll (master → node)

```
C3  <node>  00  <ck1>  <ck2>          5 bytes
```

`ck1`/`ck2` is the checksum (section 2.3) computed over the three preceding
bytes `[C3, node, 00]`. ✅ Verified exactly for node 1 (`01 00 C4 4B`) and
node 4 (`04 00 C7 51`).

### 2.2 Response (node → master)

```
<TYPE>  <node>  <LEN>  <bank>  <reg>  <data …>  <ck1>  <ck2>
```

**The response does not repeat the `C3` header.** That header belongs to the
poll. Sending it means impersonating the master, and the CS50 ignores you. ✅

- `LEN` counts from the `bank` byte, so the payload is `LEN − 2` bytes.
- `ck1`/`ck2` covers everything from `TYPE` up to but not including the
  checksum itself.

### 2.3 Checksum

A Fletcher-style running sum, both bytes modulo 256:

```c
uint8_t s1 = 0, s2 = 0;
for (byte b : data) { s1 += b; s2 += s1; }
```

✅ Validated over 23,708 sniffed bytes: 766 frames, zero false positives.
Length plus checksum is therefore a safe frame detector — you do not need to
track bus state to find frame boundaries.

### 2.4 Sniffer's view

A passive listener sees poll and response back to back, which looks like one
frame:

```
C3 01 00 C4 4B   C1 01 16 20 0E  <22 data bytes>  <ck ck>
└── poll ─────┘  └── response ──────────────────────────┘
```

This is why an 8-byte-header model appears to work for reading and then fails
completely for writing.

---

## 3. The bus is polled

This is the central fact about the protocol.

The CS50 is the master. It polls one node at a time, and **only the addressed
node may transmit**. Unsolicited frames are ignored no matter how byte-perfect
they are. ✅

### 3.1 Node addresses

| Node | Role |
|---|---|
| 1 | CS50 control board (the master's own data source) |
| 2, 3 | Probed at startup, unused on our installation |
| 4 | CI50 panel 1 |
| 5 | Panel 2 — **inferred**, see below 🟡 |
| 0x41 (65) | Polled, unexplained — see below ❓ |

**What is measured, and what is inferred.** We have measured that answering as
node 5 works, that it survives our own restarts, and that no physical second
panel is needed for the CS50 to poll it. Calling node 5 "panel 2" is the
inference: the CI 50 manual states that switch 3 must be set differently on each
panel when more than one is fitted, and our panel — factory default, switch 3
off — is node 4. Nobody has set a physical panel to panel 2 and observed it
appear as node 5, so the mapping between that switch and this address is
reasoning, not observation. It has no practical consequence for the
integration: node 5 is free and answering on it works.

The address space is not closed at 5. Checksum-valid polls to node `0x41` have
been captured twice, each a few minutes after a restart of our node (at 7.5 and
3.9 minutes).

**Do not read "twice" as "rare".** Both sightings came from the anomaly log,
which reports a frame signature only on its *first* appearance after each boot —
so a second occurrence is something it cannot report by construction. The
honest statement is that `C3 41` is polled at least occasionally and may well be
a regular member of the poll round; characterising it needs raw frame logging
over a stretch of time, not the anomaly log. An earlier draft of this document
claimed it was "not repeated in the 21 hours that followed", which was an
artefact of the detector rather than an observation.

It is harmless either way — we answer only for our own node.

### 3.2 Enumeration — and why it matters

At CS50 startup the master polls nodes 2, 3, 4 and 5. **A node that does not
answer within five polls is dropped for the rest of the run.** ✅

Measured on a real cold start (both unit and node powered from the same
supply):

| Node | Polls received | Outcome |
|---|---|---|
| 2 | **5** — all within 371 ms | dropped |
| 3 | **5** — all within 394 ms | dropped |
| 4 (panel) | 38 and counting | kept |
| 5 (us) | 38 and counting | kept |

The poll round runs `1, 2, 1, 3, 1, 4, 1, 5, …`; the node-1 entries are the
master's own address header in front of each data block, not real polls.

**Practical consequence:** if your node is not listening during that ~400 ms
window, every write will fail *silently* until the unit is power-cycled. The
integration exposes this as a binary sensor, and you should alert on it.

The margin exists because the CS50's own boot is slower than an ESP32's. That
is a property of the hardware, not luck — but it is under half a second, so do
not rely on it blindly.

### 3.3 Idle response

When a node has nothing to say it still must answer. The CI50 replies:

```
C0 <node> 02 22 00 <ck ck>
```

✅ Emitting the same is sufficient to stay enumerated.

---

## 4. Frame types

| Type | Payload encoding | Content |
|---|---|---|
| `0xC0` | none (bank/reg only) | "nothing new" idle reply ✅ |
| `0xC1` | raw bytes | status telegram, panel state frame, ASCII version strings ✅ |
| `0xC2` | IEEE754 float, little endian, 7 per frame | live measurements ✅ |
| `0xC6` | 16-bit integers, **big endian**, 14 per frame | parameter tables, clock storage ✅ |
| `0xC7` | IEEE754 float, little endian | float parameters and limits ✅ |

Note the endianness split: `0xC2`/`0xC7` floats are little endian, `0xC6`
integers are big endian (`00 19` = 25). That is unusual but consistent.

**Banks:** `0x20` = operation and parameters, `0x21` = clock/schedule storage,
`0x22` = device identity.

**Register indices count in blocks**, not bytes: they step `0x00, 0x07, 0x0E,
0x15, 0x1C` for floats (7 per frame) and `0x00, 0x0E, 0x1C` for integers
(14 per frame).

### 4.1 `0xC0` is not a read request

Tested and rejected: of 27 `C0` frames observed, **zero** were followed by a
reply carrying the same bank/reg. ✅ (negative result)

**There is no known way to request a specific register.** You do not need one:
the CS50 broadcasts its entire register set continuously in a fixed round of
15 blocks, so every value arrives within a couple of seconds regardless.

---

## 5. The status telegram

`0xC1`, node 1, `LEN` = 22, bank `0x20`, register `0x0E`. Sent every 0.7–1.2 s.

Indices below are into the payload, where `[0]` is the bank byte.

| Idx | Meaning | Confidence |
|---|---|---|
| 0 | `0x20` — bank | constant |
| 1 | `0x0E` — register | constant |
| 2 | bit0 = **heat recovery running**; bits 2–4 and 5–7 = fan relay feedback, two one-hot groups (level 3/2/1) for supply and extract; bit1 see below | ✅ |
| 3 | `0x80` | constant |
| 4 | **alarm bit field**; bit1 (`0x02`) = filter alarm | ✅ for bit1 |
| 5 | **fan level, two nibbles**: high = running, low = return. `0x31` = boost | ✅ |
| 6 | bit0 (`0x01`) = **boost active**; bit7 (`0x80`) = **afterheater enabled** | ✅ |
| 7 | `0x04` | constant |
| 8 | `0x00` | constant |
| 9 | **heat exchanger setpoint**, °C (15–25) | ✅ |
| 10 | `0` | constant |
| 11 | **heat demand**, 0–100, drives the rotor (J5 pin 11,12) | ✅ |
| 12 | `0` | constant |
| 13 | **supply fan duty**, % (49 / 74 / 100) | ✅ |
| 14 | **extract fan duty**, % | ✅ |
| 15 | `32 / 35 / 48 / 51` — varies, no known correlate | ❓ |
| 16, 17 | `0` | constant |
| 18, 19 | `0x98`, `0x88` | constant |
| 20 | `0x88` normal, `0x44` during boost | ✅ correlate, meaning unknown |
| 21 | `0` | constant |

### 5.1 Fan level is two nibbles

`[5]` high nibble is the level actually running; low nibble is the level the
unit returns to when boost ends. `0x11`/`0x22`/`0x33` are steady states,
`0x31` means "running level 3, will fall back to 1" — i.e. boost.

Dividing the byte by 17 happens to work for the steady states and breaks on
boost. Use `raw >> 4` and `raw & 0x0F`. ✅

**Boost detection comes free:** high nibble ≠ low nibble.

### 5.2 `[6]` — two independent bits, and three wrong answers

This field took three attempts to read correctly, and the failure mode is
instructive enough to record.

| Reading | Fate |
|---|---|
| "bit0 = the element is heating now" | ❌ Disproved: bit0 was set during boost while the afterheater was demonstrably off |
| "bit7 = afterheater *disabled*, inverted" | ❌ Disproved: enabling the afterheater sets the bit |
| "bit7 is not the enable flag at all" | ❌ Also wrong — it appeared in both states only because *our own writes* were switching the afterheater off between measurements |

The correct reading, verified in both directions by varying one thing at a
time: **bit0 = boost, bit7 = afterheater enabled**. ✅

There is consequently **no "element is heating now" indicator on the bus**,
even though the CI50 has a dedicated LED for it. That remains an open question.

### 5.3 `[2]` — relay feedback, not a computed value

Bits 2–4 and 5–7 are one-hot groups reporting which fan speed relays are
pulled, for supply and extract separately. Verified against 592 telegrams with
zero mismatches against `[5]`'s high nibble. ✅

The value `0` appears briefly during a level change, while no relay is
engaged. If the two groups ever disagree, the fans are genuinely running at
different speeds — a diagnostic signal not otherwise available.

**Bit1 has never been observed set** in 837 telegrams. Two hypotheses fit that
equally well, and they were not distinguished until now: ❓

1. **Bypass.** Flexit uses the same output (J5 pin 11,12) for "rotor **or**
   bypass motor" depending on unit type, so on a plate-exchanger unit this
   group would encode bypass — absent on our rotary unit by construction.
2. **The heating relay.** The CS 50 drives an *electric* afterheater from relay
   outputs ("Varme trinn 2 (el.batteri)"); the J5 0–10 V afterheater signal is
   designated for the *water* battery's valve motor, and the PWM/SSR output for
   electric elements (J6 pin 13,14) is marked "not CS 50". `[2]` is the relay
   byte, so the element's state belongs here. It has simply been summer.

Hypothesis 2 also supplies the "element is heating now" indicator we otherwise
lack. Both predict "never set so far", so only a measurement separates them:
run the afterheater with a real heat demand and see whether bit1 sets. See
TODO.md.

### 5.4 `[15]` — two nibbles, and the low one is boost

`[15]` was long the only status field with no known correlate, "varying between
32/35/48/51". Those are `0x20`, `0x23`, `0x30`, `0x33`: it is **two nibbles**,
not four arbitrary values. ✅

**Low nibble = the panel's boost REQUEST — not whether the unit is boosting.**
🟡 It follows the `0x14` command exactly (3 after an "on", 0 after an "off"),
but it can sit at `3` while the unit is demonstrably not boosting: observed
2026-08-16 with `[15] = 0x33`, `[6]` bit0 clear and the fan at level 1, for
minutes on end, after the panel issued boost requests that the CS 50 did not
act on.

An earlier draft of this section claimed the low nibble tracked the running
state. That was wrong, and it was wrong for the usual reason: the first capture
happened to be one where request and state moved together. **`[6]` bit0 is the
authority on whether boost is running.**

The high nibble takes `2` or `3` and is not settled. It stayed `3` throughout a
capture in which the afterheater was on the whole time, and the one archived
command frame carrying `2` comes from the period when our own mirroring bug was
switching the afterheater off — so "high nibble = afterheater" fits the
evidence, but no controlled test has been run. ❓

**`[15]` is owned by the `0x14` command (§7.4), not by the fan state.** Writing
a fan level ends a boost and clears `[6]` bit0 but leaves `[15]` at `0x33`;
only the command moves it. That discrepancy is what revealed the command. Note
that the panel leaves `[15]` and the running state disagreeing too, so a
mismatch is not by itself evidence of a bug in your own writes.

### 5.5 Boost requests are dropped for ~3 minutes after a boost ends

A boost request issued shortly after a previous boost ended is **silently
discarded by the CS 50**. 🟡 Measured 2026-08-16 in a controlled run, one
operation at a time:

| Time since previous boost ended | Command on the bus | Fans |
|---|---|---|
| 69 s | `20 14 31 33` — sent, `[15]` → `0x33` | **did not start** |
| 263 s | `20 14 31 33` — identical | started |

Consistent with the 180 s "shutdown sequence" (`0x0E[4]`), though only these two
points bracket it.

**Two properties make this nastier than a plain lockout.** The request is *not*
queued: `[15]` stayed at `0x33` for three minutes and the fans never started,
not even once 180 s had elapsed. And because the panel toggles on `[15]`, the
dropped request leaves the panel believing boost is on — so **the next press
sends "off", and it takes two presses to get going again.** From the operator's
chair this is indistinguishable from a broken button, and it was in fact
misdiagnosed as one; only the bus log separated "press never registered" from
"press registered and ignored". Every press in that session did reach the bus.

Implementations should therefore **verify that boost actually engaged** rather
than assume the command took effect, and should send absolute on/off values
rather than toggling, which avoids the two-press recovery entirely.

---

## 6. The panel state frame

`0xC1`, node 4, `LEN` = 8, bank `0x20`, register `0x0F`. Sent **only on
change** — it can be hours stale.

```
20 0F <d2> <fan> <flags> 04 00 <setpoint>
```

| Field | Meaning |
|---|---|
| `data[3]` | fan. As *state*: `0x11`/`0x22`/`0x33`. As a *command*: `(from, to)` — `0x12`, `0x23`, `0x32`, `0x21` ✅ |
| `data[4]` | bit7 = afterheater enabled; bit6 = momentary button bit; `0x01` = boost button; `0xC0` = both temperature buttons (the filter-reset gesture) ✅ |
| `data[7]` | setpoint, `0x0F`–`0x19` (15–25 °C) ✅ |
| `data[2]`, `[5]`, `[6]` | unknown. `[5]` is constant `0x04`, `[6]` constant `0x00` ❓ |

### 6.1 Trap: index `[4]` means two different things

In the **status telegram** `[4]` is the alarm bit field. In the **panel state
frame** `data[4]` is button events. Different register blocks, same index.
This has caused real confusion; check which frame you are looking at.

---

## 7. Writing

All writing is done by **answering a poll addressed to your node**. There is no
other channel.

### 7.1 The mirroring rule — read this before writing anything

> **Never build an outgoing frame from scratch. Start from the last state you
> received and override only the field you actually mean to change.**

The panel state frame carries several fields in one message. Hardcoding the
ones you are not changing will overwrite reality. This project got that wrong
**four separate times**, and each time the symptom was the afterheater
switching itself off:

1. Hardcoded `data[2] = 0x02` → every write re-enabled the afterheater.
2. Hardcoded `data[4] = 0x00` → every setpoint or fan write disabled it.
3. Read the enable flag from the *panel* frame, which is only sent on change →
   after a restart the mirrored value defaulted to "off", so the first fan
   command after every boot switched the afterheater off. Fixed by reading it
   from the status telegram instead, which arrives every second.
4. `trigger_boost()` built its state frame from scratch → every boost press
   switched the afterheater off.

The rule is cheap. The bugs were not.

### 7.2 Verified writes

| Operation | Frame | Status |
|---|---|---|
| Setpoint | `C1 <n> 08 │ 20 0F <d2> <fan> <flags> 04 00 <°C>` | ✅ confirmed by the CS50's own float register following |
| Fan level | same frame, `data[3]` = `(prev << 4) │ new` | ✅ duty went 49 % → 74 % |
| Boost | **two** queued frames: a state frame with `data[4] = 0x01`, then `C1 <n> 04 20 14 31 23` | ✅ |
| Cancel boost | write the return level (low nibble of `[5]`) | ✅ duty 100 % → 49 % |
| Afterheater on/off | state frame with `data[4]` bit7 set/cleared | ✅ verified both ways against the panel LED |
| Filter reset | three state frames: setpoint→20, then `data[4]` button bit, then restore setpoint | ✅ frames accepted; timer reset verifiable via the filter counter |

**Boost needs both frames.** Sending only the command frame produces no
reaction, even though it is byte-identical to the panel's and demonstrably
reaches the bus.

**A fan command during active boost cleanly cancels it.** ✅ `[5]` goes
`0x32` → `0x11`, duty 100 % → 49 %, no odd intermediate state. The command's
"from" nibble is 3 during boost and the CS50 accepts that without complaint.

### 7.3 Timing

Transmit only after **measured silence** on the bus. A fixed delay after a
completed frame collides, because the CS50 begins its next telegram before
that. 5 ms of silence is about 10 character times at 19200 baud; observed gaps
between telegrams are 20–55 ms.

When answering polls, respond immediately on recognising the poll — do not
wait for a quiet window, you already have one.

### 7.4 Boost is register `0x14`, and the timer is in the PANEL

Boost on and off are the same 4-byte command with one nibble different: ✅

```
C1 <node> 04 20 14 <duty> <flags>      duty   = current fan duty (0x31/0x64)
                                       flags  = [15] with low nibble 3=on 0=off
```

Both bytes are mirrored from the live status telegram. Writing a **fan level**
also ends a boost — that is what this integration used to do — but it leaves
`[15]` stuck at `0x33`, because only this command clears it.

**The 30-minute period is timed by the CI 50 panel, not by the CS 50.**
Measured 2026-08-16 with two runs that differ in exactly one thing:

| Boost started by | Duration | How it ended |
|---|---|---|
| the panel (one press) | **30 min 24 s** | the **panel** sent `20 14 64 30` itself |
| us, over the bus | **36 min, still running** | never — we cancelled it manually |

In the second run the panel transmitted nothing at all for 37 minutes: no state
frame, no command. It does not time what it did not start.

Two consequences for anyone implementing this. **A boost you start over the bus
runs until something cancels it** — you must keep your own timer, or the fans
stay at maximum indefinitely.

**And the press count for 60/90 minutes is not on the bus at all** — although
the longer period is entirely real. ✅ Three measurements settle it:

| Selection | Command on the bus | Measured duration |
|---|---|---|
| one press | `20 14 31 33` | 30 min 24 s |
| one press | `20 14 31 33` | 30 min 25 s |
| **two presses** (2 LEDs, confirmed on the panel) | `20 14 31 33` — **identical** | **60 min 51 s** |

So the panel really does keep a 60-minute clock; it simply never says so. The
overshoot scales with the period (+1.33 % and +1.42 %), which is one slow clock
rather than a fixed overhead — a small sign that both numbers measure the same
mechanism.

The consequence is sharper than "we cannot set the duration": **we cannot know
it either.** When a boost begins at the panel, the bus reveals that it started
but nothing about when it will end, so an integration cannot predict the end
time of a boost it did not start. Only boosts you start yourself have a
duration you know.

Two practical notes from that session. Pressing while a boost is running
**deactivates** it rather than extending it, so a longer period must be selected
from an idle state. And the CS 50 does not honour every request: two requests
issued 2 s and 22 s after a boost ended set `[15]` without starting the fans
(§5.4).

---

## 8. Measurements (`0xC2`)

| Reg | Slot | Value |
|---|---|---|
| 0 | 0 | `-55` — spare sensor input, not connected ✅ |
| 0 | 1 | **supply air temperature** (Flexit's B1) ✅ |
| 0 | 4 | `-55` — spare sensor input ✅ |
| 7 | 1 | setpoint as a float — a useful cross-check on status `[9]` ✅ |

**`-55` means "input not connected".** Translating it to NAN gives
`unavailable` in Home Assistant, which is exactly what it means — and doubles
as free hardware detection: entities for options you do not have hide
themselves.

**There is only one temperature sensor on the bus.** Extract, exhaust and
outdoor air are not measured by the CS50; the manual marks them "not CS 50".
The `-55` slots correspond to B6, the plate-exchanger frost sensor, which does
not exist on a rotary unit.

---

## 9. Parameters (`0xC6` and `0xC7`)

These are the parameter tables from the Flexit manual, broadcast continuously.

### 9.1 The register map is the CS 500's

Confirmed, not merely suspected: the cooling parameters `45` (minimum speed)
and `180` (time between starts), both marked "not CS 50" in the manual, sit in
the registers of a board that has no cooling at all. ✅ Unused parameters are
simply left at factory defaults.

### 9.2 Grouped by type, not by menu order

Matching the full CS 500 parameter list against the registers recovers 36 of 38
factory defaults — but the *order* is not the manual's. The longest in-order
subsequence is only 4 values, while **17 of 37 adjacent pairs in the manual are
adjacent bytes in the registers**. Min/max pairs cluster together.

The clearest evidence is six consecutive bytes spanning three separate manual
sections:

```
10 23  10 23  0F 02
16,35  16,35  15,2
```

Beware of over-reading single-value matches: only 25 distinct byte values occur
across the whole block, and a null control recovers 29 of 38 by chance. It is
the *ordered pairs* that carry the proof.

### 9.3 Identified fields

Bank `0x20`, word indices within each register block:

| Reg | Word | Meaning | Value |
|---|---|---|---|
| `0x00` | 0 (low byte) | maximum setpoint | 25 ✅ |
| `0x00` | 9 | min / max supply air temperature | 16 / 35 ✅ |
| `0x00` | 11 | outdoor compensation: temperature / deviation | 15 / 2 ✅ |
| `0x0E` | 0 (high byte) | minimum setpoint | 15 ✅ |
| `0x0E` | 4 | shutdown sequence | 180 s ✅ |
| `0x0E` | 5 | **filter interval** | 6 months ✅ |
| `0x0E` | 6 | motor protection delay | 30 s ✅ |
| `0x0E` | 8, 9 | **fan duty per level**, supply and extract | 50 / 75 / 100 % ✅ |
| `0x0E` | 12 | **stored user settings**: setpoint (high) and fan level (low) | ✅ |
| `0x1C` | 8 | **an hour counter tied to the filter** | ticks +1 per hour ✅, but what it counts *from* is unsettled 🟡 |

The stored-settings word was identified by watching it follow a panel session
byte for byte as the user swept setpoint and fan level — it is what makes the
unit remember its settings across a power cut.

**The filter counter is the most promising of these, and the least understood.**
It increments once per hour, reliably. What it counts *from* is another matter,
and an earlier draft of this document claimed too much by saying that it gives
"time until filter change" when combined with the interval.

It does not, on the evidence we have. Measured 2026-08-16: the counter stood at
**29,418 h** against a filter interval of **6 months** — call it 4,400 h — so a
subtraction would put the filter three years overdue. The filter alarm was
**off**. And when the alarm did fire and clear (on 2026-08-14 at 23:44 and
2026-08-15 at 00:19), the counter ran straight through the event without
resetting.

Three readings survive that, and we cannot yet separate them: ❓

1. The counter runs from the last *genuine* filter reset, and none of our
   attempts have been one.
2. It counts something else entirely — operating hours since some earlier
   event. It is not lifetime hours: 29,418 h is 3.4 years, and the panel's
   board carries a 2006 date code.
3. The alarm threshold is not `interval × hours-per-month`, so the two fields
   are not directly comparable at all.

**What would settle it:** a real filter change with the documented reset
procedure, watching whether the counter zeroes — the integration now reports any
parameter word that moves, so the answer arrives by itself. Failing that, a
register holding the threshold in *hours* would make the subtraction honest.
See TODO.md.

#### The fan duty parameters do not control our fans

`0x0E` words 8 and 9 read 50 / 75 / 100 %, and the broadcast duty `[13]`/`[14]`
follows them (49 / 74 / 100 — consistent with the percentage being scaled
through a byte and back). It is tempting to conclude that writing them would
re-balance the ventilation. **On this unit it would not**, and the reason is
worth recording before anyone tries.

The CS 50/CS 500 manual (94269N-02 §4.49–4.51) attaches an explicit condition
to these parameters: *"Dette gjelder bare for aggregater som har trinnløst
regulering av viftene"* — they apply only to units with **stepless** fan
control. The SL4 R is transformer-regulated: the CI 50 manual (110191N-07 p. 5)
balances it with a **physical switch on the transformer**, set per fan, with
voltage taps (120/150/170 V on level 2), and states that levels 1 and 3 have
*fixed* transformer settings that can only be changed by rewiring the
transformer itself.

Our own capture data says the same thing independently: `[2]` is one-hot
**relay** feedback for supply and extract fan speed (§5.3, 592 telegrams, zero
mismatches), and the CS 50 terminal list has matching relay outputs
("Tilluftsvifte hastighet 1/2/3, Relè utgang"). Relay-switched taps, not a
modulated signal. So `[13]`/`[14]` is a *nominal* figure derived from the
parameter, not a measurement of what the fans are doing.

Two conclusions follow. First, on a transformer-regulated unit these registers
are inert CS 500 heritage — writing them would change a reported number at
best. Second, even on a stepless unit they should be left alone: they are
**commissioning data**, the balance point of the whole duct system, which the
CI 50 manual requires to be set from *"Dokumentasjon av ventilasjonsdata"*
supplied by the design engineer. That is not a runtime control.

Recorded here so the factory values survive any future experiment:

| Level | Supply | Extract | Reported duty `[13]`/`[14]` |
|---|---|---|---|
| 1 | 50 % | 50 % | 49 % |
| 2 | 75 % | 75 % | 74 % |
| 3 | 100 % | 100 % | 100 % |

### 9.4 `0xC7` floats

| Reg | Slot | Meaning |
|---|---|---|
| `0x07` | 3–6 | summer compensation: differential 2, winter differential 1, stop 30, start 25 ✅ |
| `0x0E` | 0–2 | winter compensation: start −20, stop −30, differential 2 ✅ |
| `0x0E` | 3–6 | sensor corrections, all 0 (default) 🟡 |
| `0x00` | 0–6 | regulator gains: `0.01` ×4, `0.3` ×3 🟡 |
| `0x15` | 0–2 | `0`, `0.1`, `0.1` — unexplained ❓ |

### 9.5 A cautionary tale about counters

One 16-bit word looked like a second hour counter: three readings over three
days with a *constant* difference against the known filter counter. It was not
a counter at all. The fourth reading showed the low byte wrapping past 255
while the high byte went to `0x00` instead of carrying to `0x83`. Two
independent byte fields that happened to sit next to each other.

Three points in a row with a constant difference is not a test. It is a
hypothesis that has not yet had the chance to contradict itself.

### 9.6 Parameter registers are writable

**Confirmed.** ✅ The CS50 accepts writes to the `0xC6` parameter blocks
through the same poll-response mechanism as everything else. Verified by
changing the maximum setpoint 25 → 24 → 25; the CS50 broadcast the new value
within a second in both directions, with no other register, alarm or counter
affected.

**This is dangerous and the integration deliberately does not ship it.** The
blocks are operating parameters and by all appearances live in EEPROM. We have
shown that a *correct* write can be written back — not that a *wrong* one can
be undone. If you do this:

1. **Mirror the entire block.** Never construct a parameter frame from scratch.
2. **One byte at a time**, with the original value written down first.
3. **Read back** from the broadcast round before doing anything else.
4. **Gate it at runtime** behind an explicit switch, so a stray button press
   does nothing.
5. **Never touch the equipment configuration.** Those fields select
   rotor/plate, electric/water heating and preheat/bypass defrost. A wrong
   write reconfigures the unit for hardware it does not have.

---

## 10. Clock storage (bank `0x21`)

Structure identified, semantics not.

The 80 bytes match the manual's clock model: **day program has 4 channels,
week program has 6** — and the data is six repetitions of one pattern plus
four of another:

```
6 × (08 00 10 00 06 00 30 14)     week program 1-6
4 × (06 00 17 3B 20 14)           day program 1-4
1 × (00 00 10 00 88 88 21 09)     tail — unidentified
```

Manual defaults are recognisable: `06 00` = 06:00 (default on-time), `14` = 20
(default temperature), `17 3B` = 23:59 (range maximum), and `10`/`20`/`30`
resemble fan level nibbles. 🟡

Exact field boundaries cannot be fixed without changing a clock setting and
observing what moves — which requires a CS 500 panel with a display, since the
CI 50 has none. Now that parameter writes are known to work, writing to this
bank is the practical path; the clock is inactive on a CI 50 installation, so
the fields are comparatively safe to touch.

---

## 11. Device identity (bank `0x22`)

Eight ASCII bytes at register `0x00`. Node 1 bank `0x20` reg `0x00` gives the
controller firmware, node 4 bank `0x22` reg `0x00` the panel firmware.
Corresponds to the manual's `Test → Information → Main board / CS50 panel 1`.
✅

Please include both in any bug report — the protocol may differ between
revisions and this is the only version information available.

---

## 12. Open questions

Contributions that would settle these are very welcome; see the README.

| # | Question | What would settle it |
|---|---|---|
| 1 | Which bits in `[4]` are the rotor alarm and the overheat thermostat? | A capture from any installation while an alarm is active |
| 2 | Is `[2]` bit1 bypass — or the heating relay? | A plate-exchanger capture, **or** our own afterheater test firing the element (§5.3) |
| 3 | What is `[15]`'s HIGH nibble? | The low nibble is boost (§5.4). The high nibble takes 2 or 3; it was 3 throughout a capture with the afterheater on, and the one archived frame with 2 dates from a period when our own bug kept switching the afterheater off — suggestive, not settled |
| 4 | Where is the afterheater's own 0–10 V duty (J5 pin 9,10)? | Heating season, with a real heat demand |
| 5 | Where is the equipment configuration? | A capture from a unit with **different equipment** — diffing two installations would expose it immediately |
| 6 | What do the two moving high-byte fields in `0xC6` mean? | Long-term logging |
| 7 | Who is node `0x41`, polled once and never again? | A second sighting — anywhere. Any capture containing `C3 41` |

---

## 13. Method notes

Four things worked repeatedly, and are worth stating plainly:

**Provoke a state and watch what moves.** `[11]` sat at 0 in every capture
until the setpoint was forced to maximum; it then ramped 0 → 68 and identified
itself as heat demand.

**Vary one thing at a time.** Every wrong reading in this document came from
observing two fields that changed together. `[6]` took three attempts for
exactly this reason and five minutes to settle once varied properly.

**A negative result needs a positive control.** An early stress test looked for
"checksum failed" log lines — which are only emitted for frames that already
passed framing. Real corruption was invisible, and the conclusion drawn from
its absence was wrong.

**Verify the frame went out the right way, not just that its content was
right.** Three transmit attempts were written off on the wrong grounds because
the result was read without checking whether the frame had actually been sent
as a poll response.
