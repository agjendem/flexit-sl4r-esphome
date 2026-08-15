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
