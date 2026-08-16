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
2. **[Afterheater — provoke "Ettervarme varmer"](#the-afterheater-when-does-the-element-actually-heat)**
   with the setpoint at maximum through a cold night. **Needs the heating
   season**; it cannot succeed while heat demand never reaches 100.

---

## Decoding

### The bus

- [ ] **Who is node `0x41` (65)?** Checksum-valid polls to `0x41` have been
      caught twice on 16 August, each a few minutes after our node restarted
      (7.5 and 3.9 min). Until then the master had only been seen polling nodes
      2, 3 and 5.
      **The anomaly log cannot answer this one.** It reports a signature only on
      its first appearance per boot, so it is blind to every repeat — which
      means "seen twice" says nothing about frequency, and an earlier note here
      wrongly read it as evidence of rarity. Settle it with **raw frame logging
      left on for an hour** and count the `C3 41` polls: regular member of the
      poll round, periodic sweep, or genuinely tied to our restarts.

### Status telegram

- [ ] **Map the remaining bits in `[4]`.** The rotor alarm (B-alarm,
      self-acknowledging) and the overheat thermostat (A-alarm, requires manual
      reset inside the unit) are the documented CS 50 alarms. The overheat
      thermostat is safety-relevant and cannot be provoked safely. The
      `unknown alarm` entity plus the anomaly log will capture whichever fires
      first — **a capture from any installation with an active alarm would
      settle this immediately.**
- [ ] **`[15]` high nibble** — the low nibble turned out to be boost
      (PROTOCOL.md §5.4), which leaves only the high nibble, taking 2 or 3.
      "Afterheater" fits the evidence but has never been tested directly.
      **One controlled toggle of the afterheater while watching `[15]` settles
      it** — cheap, and safe while there is no heat demand.
- [ ] **`[20]`** — tracks boost cleanly (`0x88` normal, `0x44` during boost),
      but what the value itself encodes is unknown.
- [ ] **`[2]` bit 1** — assumed bypass, never observed set on our rotary unit.
      A plate-exchanger capture would settle it — **but so might our own
      afterheater test**, since the electric element is relay-driven and `[2]`
      is the relay byte. See the afterheater section below.
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

- [ ] **Provoke "Ettervarme varmer" — the overnight max-setpoint test.** We
      have no "the element is heating now" indicator at all; the field we
      believed was one turned out to be boost. The panel *has* the signal
      (yellow LED 6), so the bit exists somewhere.

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
- [ ] **Watch `[2]` bit1 in particular during that test — it may not be bypass
      at all.** The CS 50 terminal list drives an *electric* afterheater with
      **relay** outputs ("Varme trinn 2 (el.batteri)"), not with the J5 0–10 V
      signal, which p. 11 designates for the *water* battery valve motor
      ("Ettervarme full range vannbatteri"); the PWM/SSR output for electric
      elements (J6 pin 13,14) is marked "ikke CS 50". And `[2]` is precisely
      our known relay-feedback byte (§5.3). Bit1 has never been observed set —
      which we read as "bypass, absent on a rotary unit", but which fits
      "heating relay, and it has been summer" just as well. **The two
      hypotheses are distinguishable:** if bit1 sets when the element fires,
      it is the heat relay, and PROTOCOL.md open question 2 is answered as a
      side effect.
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
- [ ] **Explain the two moving high-byte fields** in `0xC6` (`0x0E` word 10 high
      and `0x00` word 0 high). Both are logged; look for a pattern over days.
- [ ] `0xC7` register `0x15`: `0`, `0.1`, `0.1` — still unexplained.

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

- [ ] **Confirm the filter reset actually restarts the timer.** Now measurable:
      read the filter-hours counter before and after pressing the button. A
      genuine reset should zero it. (An accidental panel reset did *not* zero
      it, which is why the alarm re-armed hours later.)
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

- [ ] **Replace the pre-push hook with a real ruleset.** `main` is currently
      guarded only by `.githooks/pre-push`, which is client-side and therefore
      advisory: it protects clones that ran `git config core.hooksPath .githooks`
      and nothing else. GitHub reserves branch protection and rulesets for
      private repositories on paid plans, so making this repository public is
      itself the fix — require the `Protocol tests` and `ESPHome config
      validation` checks, and block direct pushes. Note that a *required review*
      rule still will not fit a single-maintainer repository, since GitHub
      forbids self-approval.

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
