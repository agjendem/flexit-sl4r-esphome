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
| 2, 3 | The two **CS 500** panel slots — polled forever, never answered here 🟡 |
| 4 | **CS 50** panel 1 — our physical CI 50 ✅ |
| 5 | **CS 50** panel 2 — where we answer 🟡 |
| 0x41 (65) | Polled occasionally, unexplained — see below ❓ |

**The manual explains the address space.** `Test → Informasjon → Kontrollpaneler`
lists exactly four panel slots, in this order: *CS 500 panel 1, CS 500 panel 2,
CS 50 panel 1, CS 50 panel 2* (94269N-02 p. 21). Four slots, and four addresses
2–5, with our physical CS 50 panel answering on 4. The mapping is an inference,
but it is now supported by two independent lines of reasoning that agree, and it
explains why nodes 2 and 3 are polled on an installation that has neither: the
master offers all four slots regardless of what is fitted.

**Nodes 2 and 3 are not merely "probed at startup".** Counted over 95 s of raw
frame logging on 2026-08-20, 30 hours after our node last booted: ✅

| Node | Polls in 95 s | Interval |
|---|---|---|
| 1 | 2 125 | continuous |
| 4 | 545 | continuous |
| 2 | 6 | ~15.2 s |
| 3 | 6 | ~15.2 s |
| `0x41` | **0** | — |

The master sweeps the two empty CS 500 slots every fifteen seconds, forever,
and they never answer. An earlier draft of this document described them as
probed only at startup, which was wrong: that reading came from a cold-start
capture, where they *are* dropped from the fast round after a few polls — but
the slow sweep continues indefinitely. Node 5 does not appear in the table
because we answer it ourselves and do not log our own transmissions.

**What is measured about node 5, and what is inferred.** Measured: answering as
node 5 works, survives our own restarts, and needs no physical second panel.
Inferred: that node 5 is what the CI 50's switch 3 selects. Nobody has set a
physical panel to panel 2 and watched it appear on node 5, so that step is
reasoning. It has no practical consequence — node 5 is free and answering on it
works.

**Node `0x41` is not a member of the poll round.** ❓ It did not appear once in
the 95 s above, while nodes 2 and 3 each appeared six times, so whatever it is,
it is rarer than a 15-second sweep. Both known sightings came a few minutes
after a restart of our node (7.5 and 3.9 minutes), which keeps the "tied to
enumeration" reading alive. Note that neither sighting can be read as evidence
of *frequency*: they came from the anomaly log, which reports a signature only
on its first appearance per boot. An early draft claimed it was "not repeated in
the 21 hours that followed", which was an artefact of the detector, not an
observation.

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

**Bit1 is the electric afterheater, energised right now.** ✅

It went unobserved for 837 telegrams simply because it was summer, and two
hypotheses fit that equally well: *bypass* (Flexit uses the same output, J5
pin 11,12, for "rotor **or** bypass motor" depending on unit type) or *the
heating relay* (the CS 50 drives an electric afterheater from relay outputs,
"Varme trinn 2 (el.batteri)"). It was settled on 2026-08-19 by holding the
setpoint at its 25 °C maximum until the element had to run. Four independent
observations agree, and they rule bypass out rather than merely favouring the
alternative:

| Observation | Result |
|---|---|
| House power meter, independent of this bus | Binary **~940 W** step within 1–2 s of every edge, no intermediate levels |
| Airflow tripled (fan level 1 → 3) | Duty cycle rose **23 % → 62 %** |
| Supply air while the bit is set | **Rises**, 21.9 → 27.5 °C at setpoint 25 |
| Rotor signal `[11]` when it engages | Already saturated at **100** |

The airflow test is the decisive one. A bypass damper exists to *dump* surplus
heat, so tripling the airflow while heat demand is already saturated must make
it open *less*, not nearly three times as much. The energy figures agree from
a second direction: duty derived from metered energy on 17 August (29 %)
matches duty derived from this bit over the same day (23–26 %).

This also supplies the "element is heating now" indicator we otherwise lacked.
Note the distinction the entity names keep: `afterheat_enabled` (`[6]` bit7)
means the element is *permitted* to run; `afterheater_heating` (`[2]` bit1)
means it is drawing power at this moment.

### 5.4 `[10]` — the ramp that gates the afterheater

`[10]` sat at 0 in every early capture and was carried as a constant. When the
afterheater began cycling it started moving, and produced roughly 14 000
anomalies in two days before being reclassified. What is measured: ✅

* The element switches **on when `[10]` rises past 10** and **off when it falls
  to 4**. Reproduced on every edge observed, across setpoints 25, 24 and 20.
  This is the one rule that has never failed.
* It moves in integer steps and **rests at 0**, which is a floor.
* Rising it has taken ≈4 s per step in every observation so far. Falling is
  **not** a fixed rate: ≈5 s per step during ordinary cycling, but ≈1.4 s per
  step immediately after the setpoint was dropped 25 → 20 °C. The falling rate
  scales with how far the unit is from its setpoint.
* There is **no ceiling.** It peaked at 34 and 29 during steady cycling at
  setpoints 25 and 24, which briefly looked like a setpoint-dependent clamp —
  but after a step from 23 up to 25 it ran to 56 at that same setpoint 25.
  Those peaks are turning points, not limits. Dropping the setpoint abruptly
  does not clamp it either; it simply falls faster.
* The ramp rate is **unchanged** across the moment the element energises —
  4.0 s/step before, 4.3 s/step after. A quantity measuring a physical effect
  would have to respond when ~940 W is suddenly added; this does not. `[10]`
  is therefore a *command*, not a measurement.
* It is **not** a reading of supply air temperature: `[10]` was 0 both at
  27.1 °C and at 22.65 °C.

Taken together this behaves like an accumulator of the deviation from setpoint,
clamped at zero below, with the element gated off two thresholds on it. But the
**unit is unknown, and so is the exact control law** ❓ — in particular why the
rising rate looks constant while the falling rate does not. Its turning points
lag the supply-air crossing of the setpoint by 60–90 s, which is the right
order for sensor and transport delay, but the two measurements (61 s and 91 s)
are not consistent enough to call it settled. `[10]` remains a candidate for
the afterheater's own 0–10 V duty (open question 4).

Because it changes constantly, `[10]` is deliberately **not** in the
firmware's constant-field list; leaving it there drowned the anomaly log.

### 5.5 `[15]` — two nibbles, and the low one is boost

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

### 5.6 Boost requests are dropped for ~3 minutes after a boost ends

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
(§5.5).

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
| `0x0E` | 10 | **the filter timer** — hours since the filter was last reset | ticks +1 per hour, zeroes on reset ✅ |
| `0x1C` | 8 | a second hour counter, different epoch, never resets | ticks +1 per hour ✅, epoch unknown 🟡 |

The stored-settings word was identified by watching it follow a panel session
byte for byte as the user swept setpoint and fan level — it is what makes the
unit remember its settings across a power cut.

**The filter timer is `0x0E` word 10** — settled on 2026-08-16 by running the
reset procedure with raw frame logging on. Exactly one word changed and stayed
changed: ✅

```
before  ... 32 4B 64 00 [00 2D] 02 32 12 01 32 4B      word 10 = 45
after   ... 32 4B 64 00 [00 00] 02 32 12 01 32 4B      word 10 = 0
```

It increments **+1 per hour**, verified hour by hour across 33 consecutive
hours. The reset procedure (setpoint to 20, both temperature buttons, restore)
zeroes it.

**This corrects two earlier readings in this document, and the way they were
wrong is instructive.**

*`0x1C[8]` is not the filter timer.* It was labelled that for three days. It
survived the reset unchanged at 29,419 h. It ticks at the same rate as the real
filter timer and runs a constant number of hours ahead of it — the same tick,
a different epoch. Probably operating hours; the epoch is unknown. 🟡

*`0x0E[10]` is a normal 16-bit counter after all.* An earlier section here
argued it could not be, because it went `0x82F2` → `0x000C` without carrying
into the high byte, and concluded it must be two independent bytes. The simpler
explanation was the right one: `0x82F2` → `0` was **a reset**, performed by
accident from the panel on the night of 15 August, and `0x000C` was twelve hours
of counting afterwards. That also explains why the filter alarm cleared that
night and stayed cleared.

The lesson is the same one this project keeps relearning: a discontinuity in a
counter is more likely to be an event than a decoding error, and the way to tell
is to *cause* the event and watch.

**On "time until filter change".** With the timer identified this is a
defensible calculation — hours elapsed against an interval given in months —
but the conversion the CS 50 uses is unmeasured, so the integration ships it as
a template sensor with the assumption written next to it (730 h per month)
rather than as component code. The timer now starts from zero, so the value it
holds when the alarm next fires *is* the threshold, exactly. See TODO.md.

#### Both hour counters wrap, and sooner than the equipment lasts

Every counter here is a **single 16-bit word**, so it rolls over at 65,536
hours — **7 years and 175 days**. ✅ That is a property of the field width, not
a guess, and it is shorter than the service life of an air handling unit by a
wide margin. Any consumer of these registers has to assume a wrap will happen.

The practical consequences:

- **The absolute value means nothing on its own.** A reading of 29,419 h could
  be 3.4 years, or 10.8, or 18.3 — the wire cannot tell you which, because
  **no wrap count is broadcast.** All 78 parameter words were scanned for one;
  the only word holding a small number is the filter interval itself. 🟡
- **Deltas are still sound.** The counters tick +1 per hour and the neighbouring
  words never move, so *differences* over any period shorter than seven years
  are exact. That is what the filter timer actually needs.
- **In Home Assistant, use `state_class: total_increasing`.** A wrap looks
  exactly like a reset, which that class already handles. Anything computed on
  top will inherit the discontinuity.

**A worked example of the ambiguity**, from this installation. The house was
built in 2006–2007 and the unit stood idle for a period with a broken fan. If
`0x1C[8]` counts operating hours since commissioning, then:

| Wraps | Hours counted | Years | Implied history |
|---|---|---|---|
| 0 | 29,419 | 3.4 | counts from some event around 2023, not from commissioning |
| 1 | 94,955 | 10.8 | commissioned ~2016 — the house is older than that |
| 2 | 160,491 | 18.3 | commissioned 2007, ~16 months out of service ✅ fits |

The two-wrap row fits the building's history neatly, which is exactly why it
should be distrusted: three unknowns — commissioning date, downtime, wrap
count — against one equation will always yield a fit. It is offered here as an
illustration of the ambiguity, not as a result.

**The measurement that would resolve most of it** is whether these are
*running* hours or *wall-clock* hours: cut power to the unit across an hour
boundary and see whether the counter advances. Running hours make the downtime
explanation possible; wall-clock hours rule it out entirely. See TODO.md.

**What the field width itself tells you.** It is worth asking why a designer
would build an hour counter and then not spend the two extra bits that would
make it meaningful across the equipment's life. A 32-bit counter would run for
490,000 years; even 24 bits would cover 1,900. Sixteen was a choice.

The most likely reading is that these were never meant as lifetime counters at
all. Sixteen bits is a comfortable fit for an **interval** counter — the filter
interval maxes out at 12 months, or 8,760 h, which leaves sevenfold headroom
before a wrap can occur. `0x0E[10]` is exactly that, and its width is sensible.
`0x1C[8]` is the odd one: at 29,419 h it is already several times past any
service interval the manual documents.

Two things follow, and they matter more than the epoch does. First, **the
register map is the CS 500's** (§9.1), and this unit carries CS 500 fields it
does not use — the cooling parameters sit at factory defaults on a CS 50 that
cannot cool. `0x1C[8]` may well be another of those: incremented because the
firmware is shared, meaningful only on a different controller. Second, on a
commercial unit with a service regime, a counter that wraps every seven years is
defensible if a technician is expected to read and record it at each visit.
Neither reading makes it a lifecycle metric, and treating it as one — as this
project briefly did — is reading intent into a field that does not carry it.

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
| 2 | What is `[10]`'s unit and control law? | It gates the afterheater at 10/4 and behaves like a deviation accumulator (§5.4), but the rising and falling rates do not follow the same rule. A capture logging `[10]` against supply air across a slow setpoint sweep |
| 3 | What is `[15]`'s HIGH nibble? | The low nibble is boost (§5.5). The high nibble takes 2 or 3; it was 3 throughout a capture with the afterheater on, and the one archived frame with 2 dates from a period when our own bug kept switching the afterheater off — suggestive, not settled |
| 4 | Where is the afterheater's own 0–10 V duty (J5 pin 9,10)? | `[10]` is the leading candidate (§5.4). Against it: the element it gates is switched by a relay, binary at ~940 W, so a duty signal would have nothing to modulate on this unit |
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
