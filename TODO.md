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

## Decoding

### The bus

- [ ] **Who is node `0x41` (65)?** On 16 August the anomaly log caught a single
      checksum-valid poll to node `0x41`, about 7.5 minutes after a restart, and
      never again in the 21 hours that followed. Until then the master had only
      ever been seen polling nodes 2, 3 and 5. One sample is not a pattern:
      it could be a rare periodic sweep, something tied to the restart, or a
      node type we have not met. The anomaly log will catch the next one — the
      question is what makes it happen.

### Status telegram

- [ ] **Map the remaining bits in `[4]`.** The rotor alarm (B-alarm,
      self-acknowledging) and the overheat thermostat (A-alarm, requires manual
      reset inside the unit) are the documented CS 50 alarms. The overheat
      thermostat is safety-relevant and cannot be provoked safely. The
      `unknown alarm` entity plus the anomaly log will capture whichever fires
      first — **a capture from any installation with an active alarm would
      settle this immediately.**
- [ ] **`[15]`** — varies between 32/35/48/51, independently of both boost and
      the afterheater. The only status field with no known correlate.
- [ ] **`[20]`** — tracks boost cleanly (`0x88` normal, `0x44` during boost),
      but what the value itself encodes is unknown.
- [ ] **`[2]` bit 1** — assumed bypass, never observed set on our rotary unit.
      **Needs a capture from a plate-exchanger installation.**
- [ ] **`[12]`, `[16]`–`[19]`, `[21]`** — no variation observed. Possibly
      options we do not have, or CS 500-only fields.

### The afterheater's own duty

The CS 50 terminal list confirms both outputs exist, and neither is marked
"not CS 50":

| Terminal | Function |
|---|---|
| J5 (pin 11,12) | Control signal to heat recovery (rotor), 0-10 V |
| J5 (pin 9,10) | Control signal to the afterheater, 0-10 V |
| J5 (pin 13,14) | Rotor alarm |

- [ ] **Find the afterheater's 0-10 V duty.** We have no "element is heating
      now" indicator at all — the field we believed was one turned out to be
      boost. Best hunted during the heating season, with a real heat demand.
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

### Boost duration — and a register we may have mislabelled

The CI 50 manual (110191N-07 p. 5) gives the periods: one press = 30 min, two
= 60, three = 90, and the unit returns to the previous level by itself. The
CS 50/CS 500 manual adds three parameters, **none of them marked "not CS 50"**:

| § | Parameter | Range | Default |
|---|---|---|---|
| 4.56 | Forced ventilation → Enable | on/off | off |
| 4.57 | Forced ventilation → Standard speed | 0–3 | **3** |
| 4.58 | Forced ventilation → **Standard time** | 0–360 | **30** |

- [ ] **Find the boost-time register — and re-check `0x0E[6]`.** We labelled
      `0x0E` word 6 "motor protection delay" because it reads 30 and the manual
      gives motor protection a default of 30 s (§4.84). But boost standard time
      *also* defaults to 30 (§4.58). **The two are indistinguishable at their
      factory values**, and we never had a positive control — exactly the
      failure mode that cost us days on `[6]` and on the fake hour counter.
      The ranges differ (0–180 s vs 0–360), which does not help while both sit
      at 30.
- [ ] **Capture all three boost durations off the panel (agreed, read-only).**
      Turn on raw frame logging and press the panel's boost button once, then
      twice, then three times, letting the bus settle between each and noting
      what the panel shows. Two things to look for in the diff:
      1. **Does a parameter word change to 30 / 60 / 90?** If so, that word is
         the boost time (§4.58), `0x0E[6]` loses its "motor protection" label,
         and the duration becomes settable by writing the parameter — one
         switch plus one `number` instead of three buttons.
      2. **Or does the command frame itself differ between the presses?** We
         have only ever captured the single press
         (`20 14 31 23`). If the press count is encoded there, we need all
         three command variants and the integration offers them as a `select`.
      Either outcome is worth having; they lead to different implementations,
      which is exactly why this must be measured rather than assumed.

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
| Does the node survive a house-wide power cut? | Yes — measured. The CS50 boots slower than the ESP32, so we answer within the enumeration window |
| Can the fan duty per level (50/75/100) be written to re-balance the fans? | **No, not on this unit.** It is transformer-regulated with relay-switched taps; those parameters apply only to stepless units. See PROTOCOL.md §9.3 |
