# What remains

Two-way control works and is verified against the CS50's own broadcast values.
What is left is decoding fields we have not yet identified, plus a few
robustness items.

The findings so far are specified in [`PROTOCOL.md`](PROTOCOL.md). The
chronological derivation, including every dead end, is in
[`research/protocol-notes.md`](research/protocol-notes.md) (Norwegian); raw
captures are in [`research/captures/`](research/captures/).

**The working method:** turn on the `raw frame logging` switch, capture the log
to a file, parse it with the poll/reply validator, and *diff against a known
state*. Every finding came from changing one thing at a time and watching what
followed.

**Two method lessons worth carrying:** a negative test is worthless without a
positive control, and a plausible hypothesis deserves a measurement before it
becomes a fix. Both cost us days when ignored.

---

## Waiting on the panel

- [ ] **Triple press (90 min).** Same protocol as the confirmed double press:
      from idle, at least 3 minutes after any previous boost, three rapid
      presses, **confirm three lit LEDs**, then leave it alone. Expect
      ~90 min 76 s if the clock keeps drifting at the measured 1.4 %.
- [ ] **Does cancelling from HA stop the PANEL's timer?** Untested, and now easy
      to answer since we decode register `0x14`: start a boost at the panel,
      cancel it from Home Assistant, and watch whether the panel still sends its
      own `20 14 <duty> 30` when its period would have expired.
      Benign either way — a second cancel lands on an already-cancelled boost —
      **except for one edge case**: if you start a new boost from HA inside that
      window, the panel's pending deadline could cut it short. Worth knowing
      before anyone automates boost on a short cycle.

## The two provocation experiments

Both are agreed, both are **read-only**, and both work the same way: put the
unit into a state it will not reach on its own, and diff the bus against a
known-good capture. They are written up in place below rather than duplicated
here.

1. ~~Boost — capture all three durations off the panel.~~ **Done 2026-08-16,
   and the answer was that there is nothing to capture:** the panel counts
   presses locally and transmits only on/off, so 60 and 90 minutes are
   unreachable from the bus. It also settled the duration (30 min 24 s
   measured) and located the timer in the panel. See PROTOCOL.md §7.4.
   `0x0E[6]` never moved, so its "motor protection" label survives — still
   without a positive control.
2. ~~Afterheater — provoke "Ettervarme varmer".~~ **Done 2026-08-19, and it
   did not need the heating season after all.** Holding the setpoint at 25 °C
   was enough: heat demand `[11]` saturated at 100 within hours, and the
   element began cycling. `[2]` bit1 is the element, energised — the
   *bypass* reading is disproved, not merely outranked. A house power meter
   showed a binary ~940 W step within 1–2 s of every edge, and tripling the
   airflow raised the duty cycle from 23 % to 62 %, which no bypass damper can
   do. PROTOCOL.md open question 2 is answered; §5.3 has the evidence table.
   The test also exposed `[10]` (§5.4), which gates the element at 10/4 — and
   cost one wrong turn worth recording: its steady-cycle peaks (34 at setpoint
   25, 29 at 24) looked like a setpoint-dependent ceiling until a step change
   drove it to 56 at that same setpoint 25. It has no ceiling.

---

## Decoding

### The bus

- [ ] **Who is node `0x41` (65)?** Checksum-valid polls to `0x41` have been
      caught twice on 16 August, each a few minutes after our node restarted
      (7.5 and 3.9 min). Until then the master had only been seen polling nodes
      2, 3 and 5.
      **Partly answered 2026-08-20: it is not a member of the poll round.** A
      95 s raw capture, 30 h after our last boot, counted 2 125 polls to node 1,
      545 to node 4, six each to nodes 2 and 3 (~15.2 s apart) — and **zero** to
      `0x41`. So it is rarer than a fifteen-second sweep, which leaves "tied to
      enumeration or to our restarts" as the live reading.
      Still worth an hour of raw logging to catch one in the act. Note the
      anomaly log cannot answer it: it reports a signature only on its first
      appearance per boot, so it is blind to every repeat, and an early note
      here wrongly read "seen twice" as evidence of rarity.

### Status telegram

- [ ] **Map the remaining bits in `[4]`.** The full alarm list is in the
      CS 50/CS 500 manual (94269N-02 p. 22-23). Excluding everything marked
      "ikke CS 50" and the sensors a rotary unit does not have, these can fire
      on our hardware:

      | Alarm | Signal | Class |
      |---|---|---|
      | Collective A-alarm | — | A |
      | Collective B-alarm | — | B |
      | Supply air sensor out of range (< −45 / > +50 °C) | B1 | A |
      | Overheat thermostat tripped | BT | A |
      | Rotor alarm | RA | B |
      | Filter alarm | — | B ✅ already decoded |

      **Two of these are collective flags**, which is worth knowing before
      assuming every set bit is a distinct fault — an A-alarm and its collective
      bit would light together. The overheat thermostat is safety-relevant and
      must not be provoked. The `unknown alarm` entity plus the anomaly log will
      capture whichever fires first; **a capture from any installation with an
      active alarm would settle this immediately.**
- [ ] **`[15]` high nibble** — the low nibble turned out to be boost
      (PROTOCOL.md §5.5), which leaves only the high nibble, taking 2 or 3.
      "Afterheater **enabled**" fits the evidence but has never been tested
      directly. **One controlled toggle of the afterheater setting while
      watching `[15]` settles it.**
      Note that the 30 hours of afterheater work on 18-19 August did **not**
      settle it, tempting as it looks: `[15]` held `0x30` throughout while the
      element cycled on and off hundreds of times — but the *setting* stayed on
      the whole time, so the hypothesis was never put at risk. The element
      switching is `[2]` bit1; this is a different question.
      No longer free, either: the earlier note called it "safe while there is no
      heat demand", which was true in summer. With the setpoint high there now
      is heat demand, so toggling the setting actually stops the heating.
- [ ] **`[20]`** — tracks boost cleanly (`0x88` normal, `0x44` during boost),
      but what the value itself encodes is unknown.
- [x] **`[2]` bit 1** — ~~assumed bypass~~ **the afterheater element,
      energised.** Settled 2026-08-19 exactly as predicted below: the element
      is relay-driven and `[2]` is the relay byte. See PROTOCOL.md §5.3.
- [ ] **`[10]`** — gates the afterheater relay (on above 10, off at 4, never
      once violated) and behaves like an accumulator of the deviation from
      setpoint, floored at 0. **Unit unknown, and the control law only half
      understood:** rising has always taken ~4 s/step, while falling ranges
      from ~5 s/step in ordinary cycling to ~1.4 s/step right after a 5 °C
      setpoint drop. Logging `[10]` against supply air through a slow sweep of
      the whole 15–25 °C range would pin the law down.
- [ ] **`[12]`, `[16]`–`[19]`, `[21]`** — no variation observed. Possibly
      options we do not have, or CS 500-only fields.

### The afterheater — when does the element actually heat?

The CS 50 terminal list confirms both outputs exist, and neither is marked
"not CS 50":

| Terminal | Function |
|---|---|
| J5 (pin 11,12) | Control signal to heat recovery (rotor), 0-10 V |
| J5 (pin 9,10) | Control signal to the afterheater, 0-10 V |
| J5 (pin 13,14) | Rotor alarm |

- [x] **Provoke "Ettervarme varmer" — the overnight max-setpoint test.**
      **Done 2026-08-19.** The indicator is `[2]` bit1, now published as
      `afterheater_heating`. Note the naming distinction the entities keep:
      `afterheat_enabled` (`[6]` bit7) means *permitted to run*;
      `afterheater_heating` (`[2]` bit1) means *drawing power now*.

      **What the manual says about the cut-in point.** Supply air temperature is
      governed by one regulator output, split into zones: cooling · neutral ·
      heat recovery · neutral · heat (§4.48). The **Gjenvinner–Varme neutral
      zone defaults to 0.0 °C** (range −5…+5), so there is no dead band: the
      heat engages as soon as the recovery output is exhausted and the setpoint
      is still not met. The manual advises never going below 2 °C, which sits
      oddly with its own default of 0 — worth knowing before trusting either.

      **The precondition is therefore observable in an entity we already have:**
      heat demand (`[11]`, the rotor's 0–100 signal) must reach **100** while
      supply air is still below setpoint. Until it does, no element test can
      succeed, and that is why this cannot be forced in summer.

      Method: set the setpoint to its maximum (25 °C), turn raw frame logging
      on, and let it run through a cold night. Diff whatever moves against a
      daytime capture at the same setpoint.
- [x] **Watch `[2]` bit1 in particular during that test — it may not be bypass
      at all.** **Confirmed: it is the heating relay.** The CS 50 terminal list drives an *electric* afterheater with
      **relay** outputs ("Varme trinn 2 (el.batteri)"), not with the J5 0–10 V
      signal, which p. 11 designates for the *water* battery valve motor
      ("Ettervarme full range vannbatteri"); the PWM/SSR output for electric
      elements (J6 pin 13,14) is marked "ikke CS 50". And `[2]` is precisely
      our known relay-feedback byte (§5.3). Bit1 has never been observed set —
      which we read as "bypass, absent on a rotary unit", but which fits
      "heating relay, and it has been summer" just as well. **The two
      hypotheses are distinguishable:** if bit1 sets when the element fires,
      it is the heat relay, and PROTOCOL.md open question 2 is answered as a
      side effect. — *That is exactly what happened.*
- [ ] **Then find the modulating duty, if it exists.** Should the element turn
      out to be stepped rather than modulated on this unit, "how hard is it
      heating" may not exist on the bus at all — only "which step is engaged".
- [ ] **Find the rotor alarm bit.** The rotor has a built-in self-test that
      runs for one minute daily; logging over 24 h should reveal it.

### Parameter registers

- [ ] **Decode the clock storage (bank `0x21`).** Structure is known (4 day
      channels + 6 week channels, with recognisable defaults), semantics are
      not. Now practical, since parameter writes are confirmed to work: write
      one field and see what moves. The clock is inactive on a CI 50
      installation, so these fields are comparatively safe to touch.
- [ ] **Find the equipment configuration.** The CS 50 knows its own equipment —
      it is set with three microswitches (rotor/plate, electric/water heating,
      preheat/bypass defrost). Three bits. Finding them would let the
      integration configure itself instead of every user having to know what
      they have.
      **This can only be settled by diffing two installations with different
      equipment.** It must NEVER be probed by writing — a wrong write
      reconfigures the unit for hardware it does not have.
- [ ] **Explain the moving high byte of `0xC6` `0x00` word 0.** The low byte is
      the maximum setpoint (25); the high byte reads 8 and has been seen to move.
      ~~`0x0E` word 10 high~~ is no longer part of this: that word is the filter
      timer in full, a plain 16-bit counter, so its high byte moves for the
      obvious reason — the count passed 255.
- [ ] `0xC7` register `0x15`: `0`, `0.1`, `0.1` — still unexplained.
- [ ] **Is there a serial number, and can it be recovered from the bus?**
      Worth having: it would date the unit independently, and could settle the
      epoch of the `0x1C` word 8 hour counter, which is currently guesswork
      anchored on the house being built in 2006-2007.

      **What is already known: no serial number is broadcast.** A capture
      covering all 17 frame shapes was scanned for printable runs, and the only
      ASCII anywhere on the bus is the two firmware strings — `R1A 2.8`
      (controller, bank `0x20` reg `0x00`) and `R1A 1.2` (panel, bank `0x22`
      reg `0x00`). Both sit in 28- and 8-byte fields; the controller's is
      followed by 18 zero bytes, so there is room reserved next to it that
      carries nothing here.

      **The bus route is closed, per the manual.** `Test → Informasjon` is where
      the firmware strings come from, and the manual lists its entire contents
      (94269N-02 p. 20-21): main board hardware and software revision, the same
      two for each of four panel slots, I/O, factory parameters, priorities,
      hour counter, filter time and the alarm history. **No serial number, no
      production date — on either panel type.** Searching the CS 50/CS 500
      manual for "serienr", "Serial" and "art.nr" returns nothing at all.

      That matters because the CS 50 polls a fixed set of registers and we
      cannot ask for others — `0xC0` is not a read request (PROTOCOL.md §4.1).
      A field the panel never displays is a field nothing provokes onto the bus,
      so there is no capture that would reveal it.

      **The panel is done — and it has no serial number.** Read 2026-08-20, the
      CI 50 wall panel's board is marked `Flexit AS 55412 R2C 060314`: an
      article number in Flexit's five-digit format, a revision code, and a date
      code. Exactly the outcome this item anticipated for a unit of this age.
      Written up in PROTOCOL.md §11.

      **What is left is the control board inside the unit.** That is the one
      that matters: the hour counters live there, not in the panel, so only its
      marking can date them. A panel can be replaced without touching it — and
      the panel's own date code is ambiguous (`YYMMDD` gives March 2006,
      Norwegian `DDMMYY` gives March 2014), which is precisely why the panel
      cannot settle the epoch. Note every number on the control board, and if
      it carries a date code in the same shape, whether it agrees with the
      panel's — two boards reading 06 and 14 would say the panel was replaced.
- [ ] **Read the hardware revision, not just the software revision.** The
      manual's information menu offers *Maskinvare rev.* alongside
      *Programvare rev.* for the main board and for every panel slot, but we
      decode only one 8-byte ASCII string per node (`R1A 2.8` and `R1A 1.2`) —
      which, given the menu, are the software ones. So a second string exists
      somewhere and we have not found it. Since the panel displays it, it must
      cross the bus, which makes this the *opposite* case to the serial number
      above and therefore findable: capture while walking that menu on the
      panel. Worth having in bug reports, where a hardware revision would
      distinguish board variants that a firmware string alone cannot.

      **We now know what to look for.** The panel's board is marked `R2C` while
      the panel reports `R1A 1.2` on the bus — same `R<digit><letter>` shape,
      different value. So search a capture taken while walking that menu for the
      ASCII `R2C`; finding it identifies the field outright, and *not* finding
      it is nearly as informative, since it would mean the hardware revision
      never leaves the panel.

### Boost — answered 2026-08-16

The press count is **not on the bus**. The panel counts presses locally and
transmits only on/off, so the 60- and 90-minute options cannot be reached from
the bus; every "on" command is byte-identical. The 30-minute period was measured
at 30 min 24 s, and the timer turned out to live in the CI 50 panel, which sends
the cancel itself. Full write-up in PROTOCOL.md §7.4; the integration now keeps
its own timer for boosts it starts.

Two loose ends survived:

- [ ] **`0x0E[6]` still lacks a positive control.** It never moved during the
      test, so "motor protection delay" stands — but the rival reading (§4.58
      boost standard time) was never *disproved*, only left untested, since the
      panel does not write the duration anywhere. Both default to 30. If a
      register write is ever attempted on the experiment branch, this is the
      one to change and watch.
- [ ] **Offer 30/60/90 minutes ourselves.** Now clearly worth doing. The panel's
      longer periods are real — a confirmed double press ran **60 min 51 s**
      against 30 min 24 s for a single one — but the duration never reaches the
      bus, so we cannot reproduce them by sending a different command. We can
      reproduce them with our own timer, since the unit stays in boost until
      cancelled. A `number` (or a `select` of 30/60/90) driving `BOOST_PERIOD_MS`
      would give parity with the panel. Note it would be *our* clock, not the
      panel's.
- [ ] **We cannot predict the end of a PANEL-started boost.** The bus shows that
      one began but not how long it will last, so any "boost ends at" display
      would be a guess for boosts we did not start. Worth stating in the UI
      rather than papering over.

## Testing

Run them with `./tests/run.sh` — a C++17 compiler and nothing else. See
[`tests/README.md`](tests/README.md).

The byte-level logic now lives in `components/flexit_sl4r/protocol.h`, free of
ESPHome, so the tests exercise the code the firmware actually runs. 108 checks
cover the checksum (vectors read off the wire), frame validation and
resynchronisation, the `[5]` nibbles, the boost command's mirroring of `[15]`,
the big-endian `0xC6` words, and a replay of both recorded captures asserted
against what PROTOCOL.md says they contain.

- [ ] **Extend to the parts that still have none.** Untested: enumeration, the
      transmit path, the boost timer and the CS 50's *responses* — all of which
      need hardware or elapsed time. A recorded-capture harness that could
      replay a whole session against the component (not just `protocol.h`) would
      reach most of it, and is the obvious next step.
- [x] ~~Run them in CI.~~ Done — `.github/workflows/tests.yml` runs the suite on
      every push and pull request, alongside an `esphome config` validation of
      `example.yaml` that catches a platform schema drifting from the YAML it
      documents. Private repositories get Actions minutes too, so this did not
      have to wait for publication.

## Verification

- [x] ~~Confirm the filter reset actually restarts the timer.~~ **Done
      2026-08-16, and it found a mislabelled register.** The reset works; it
      zeroes `0x0E[10]`, which is the real filter timer. `0x1C[8]`, which this
      project had called "filter hours" for three days, is a different counter
      on a different epoch and does not reset. See PROTOCOL.md §9.3.
- [ ] **Measure the filter alarm threshold and drop the assumed conversion.**
      The "filter change due in" sensor assumes 730 h per month, because the
      interval is given in months and the timer counts hours. The timer was
      zeroed on 2026-08-16, so **the value it holds when the alarm next fires is
      the threshold in hours, exactly**. Expect the answer in roughly six
      months; the anomaly log records the alarm changing state whether or not
      anyone is watching. Replace the constant then, and the sensor becomes
      exact instead of approximate.
- [ ] **What is `0x1C[8]`'s epoch, and do these counters track running hours or
      wall-clock hours?** The wrap behaviour and the ambiguity it creates are
      documented in PROTOCOL.md §9.3, with a worked example from this
      installation; what is missing is a measurement.

      **Cut power to the unit across an hour boundary** — the tick lands around
      :51–:58 — and see whether the counter advances. If it does not, these are
      running hours and a unit's downtime shows up as a shortfall against
      calendar time. If it does, the CS 50 keeps a real-time clock and the
      epoch is something else entirely. Roughly twenty minutes with the
      ventilation off; the wall socket is enough, no need to open anything.

      A second installation with a known commissioning date and no downtime
      would settle the epoch outright, since the counter would then have to
      equal the age modulo 65,536.

## Verification

- [x] ~~Confirm the filter reset actually restarts the timer.~~ **Done
      2026-08-16, and it found a mislabelled register.** The reset works; it
      zeroes `0x0E[10]`, which is the real filter timer. `0x1C[8]`, which this
      project had called "filter hours" for three days, is a different counter
      on a different epoch and does not reset. See PROTOCOL.md §9.3.
- [ ] **Measure the filter alarm threshold and drop the assumed conversion.**
      The "filter change due in" sensor assumes 730 h per month, because the
      interval is given in months and the timer counts hours. The timer was
      zeroed on 2026-08-16, so **the value it holds when the alarm next fires is
      the threshold in hours, exactly**. Expect the answer in roughly six
      months; the anomaly log records the alarm changing state whether or not
      anyone is watching. Replace the constant then, and the sensor becomes
      exact instead of approximate.
- [ ] **What is `0x1C[8]`'s epoch?** Same tick rate as the filter timer, runs a
      constant offset ahead of it, never resets. "Operating hours since
      installation" is the obvious guess and **the arithmetic does not support
      it**: 29,419 h is 3.36 years of continuous running, on a unit whose panel
      board carries a 2006 date code.

      The register is 16 bits, so it wraps every 7.48 years and a remainder
      would tell you nothing about total age. But the wraps do not land
      convincingly either — 20 years of continuous running would leave 44,128,
      not 29,419, and no whole number of wraps puts the epoch near 2006:

      | Wraps | Total | Epoch |
      |---|---|---|
      | 0 | 29,419 h | ~2023 |
      | 1 | 94,955 h | ~2016 |
      | 2 | 160,491 h | ~2008 |

      **A reconstruction that arithmetically fits — and one reason to doubt it.**
      Two wraps put the counter at 18.3 years of *counted* hours. Our house was
      built 2006–2007 and the unit stood idle for a period with a broken fan;
      commissioning in 2007 with roughly 16 months out of service lands exactly
      on 18.3. On that reading `0x1C[8]` is total running hours since
      commissioning, wrapped twice, and the shortfall against wall-clock time
      is the downtime.

      **But nothing broadcasts a wrap count.** All 78 parameter words were
      scanned: the only one holding a small number is the filter interval
      (`0x0E[5]` = 6). If this counter had rolled over twice, the "2" is
      nowhere on the bus. That does not disprove wrapping — the CS 50 need not
      expose it — but it removes the only evidence that would have supported
      it, and leaves the simpler reading standing: the counter is at 29,419 and
      has never wrapped, i.e. roughly 3.4 years since *something*.

      So: three unknowns (commissioning date, downtime, wrap count) against one
      equation. A fit was always available. Treat it as a story, not a result.

      The neighbouring words are **not decoded**, but they are not part of this
      counter either: across three days in which word 8 advanced 77, every
      other word in register `0x1C` was bit-identical (25615, 0, 0, 300, 0, 0,
      300, 301, …, 100). No 32-bit pairing with a neighbour yields a sane
      number — the nearest, words 7+8, would read 2,255 years.

      **One cheap measurement would carry most of the weight:** does the
      counter track *running* hours or *wall-clock* hours? Cut power to the
      unit across an hour boundary and see whether it advances. If it does not,
      the reconstruction above stands and the counter is running hours. If it
      does, the CS 50 keeps a real-time clock, the downtime explanation
      collapses, and the epoch is something else entirely.
- [ ] **Preheat for plate-exchanger units.** Removed here because the SL4 R has
      a rotor. Needed for the integration to cover plate-exchanger variants —
      requires someone with that hardware.

## Robustness and operation

- [ ] **Automate deployment**, so the deployed copy cannot drift from the
      source.
- [ ] **Load-test the bus supply** (100 Ω across 12 V ≈ 120 mA). Low priority —
      no brownout through many OTA cycles, and `reset reason` plus `uptime`
      would catch a failure.

## For publication

- [x] ~~Replace the pre-push hook with a real ruleset.~~ Done when the
      repository went public: `main` now requires a pull request with both
      checks green, and blocks force-pushes and deletion server-side. The
      pre-push hook is kept anyway — it fails in a second locally instead of
      after a round trip to GitHub. No *required review* rule, deliberately:
      GitHub forbids approving your own pull request, so on a single-maintainer
      repository that would lock the owner out rather than protect anything.

- [ ] **Automatic hardware detection**, once the equipment configuration bits
      are found.
- [ ] **External boost switch on the CS 50** (J5 pin 16) would give an
      independent check that our boost command does the same thing the hardware
      does. Only worth it if you are opening the unit anyway.

---

## Answered along the way

| Question | Answer |
|---|---|
| Why was everything we transmitted ignored? | The bus is polled; unsolicited traffic is not listened to |
| How do we enrol? | Answer the poll for our node — enumerated at CS50 startup |
| Does the panel overwrite our writes? | No, the panel only transmits on change |
| Is dipswitch 3 needed on a physical panel? | No — we enumerate as node 5 without it |
| Which temperature sensor is on the bus? | Supply air only (Flexit's B1) |
| How many fans? | Two, not four |
| How is boost cancelled? | Write a fan level |
| What is `payload[4]`? | Alarm bit field — bit1 = filter alarm |
| What is `payload[6]`? | bit0 = boost, bit7 = afterheater enabled |
| How is the afterheater switched? | Write `data[4]` bit7, or hold minus and press plus on the panel |
| Why does the panel gesture also reset the filter? | Same button combination; duration distinguishes them. Our switch avoids it |
| Do we have preheat? | No — it is a plate-exchanger function, we have a rotor |
| Is the filter alarm pressure-based? | No — it is a timer. Pressure switches are not CS 50 |
| Is pressing both temperature buttons enough to reset the filter? | No — the setpoint must be at 20 first |
| Does rotor duty exist at all? | Yes — J5 pin 11,12, exposed as heat demand |
| One or two temperature sensors? | **One.** The afterheater kit fits two *thermostats*, not measuring sensors |
| Is `0xC0` a read request? | No — tested and rejected, 0 of 27 |
| Can the parameter registers be written? | **Yes** — confirmed. See PROTOCOL.md §9.6 before trying |
| How long does a boost last? | **30 min 24 s measured** for one press |
| Can we send the 60/90-minute options? | **No.** The panel counts presses locally; only on/off reaches the bus |
| Who times the boost? | **The CI 50 panel** — and only for boosts it started itself. A boost from the bus is never timed (measured: 36 min, panel silent) |
| What is `[15]`'s low nibble? | Boost: 3 = on, 0 = off |
| Does writing a fan level fully cancel a boost? | It returns the fan, but leaves `[15]` stale. Use the `0x14` command (§7.4) |
| Does the node survive a house-wide power cut? | Yes — measured. The CS50 boots slower than the ESP32, so we answer within the enumeration window |
| Can the fan duty per level (50/75/100) be written to re-balance the fans? | **No, not on this unit.** It is transformer-regulated with relay-switched taps; those parameters apply only to stepless units. See PROTOCOL.md §9.3 |
