# captures/ — raw bus captures from our own installation

Captured with `uart: debug:` (commented out in the example configuration) or
with the `raw frame logging` switch. The format is one line per 32 received
bytes, hex, space separated — the raw stream from the CS50/CI50, not
pre-split frames. The one exception is noted in the table below.

| File | Content |
|---|---|
| `2026-08-13-panelsekvens.hex.gz` | ~140 kB of bus traffic while the panel was operated by hand: down to fan level 1, the full setpoint sweep 17→25→15, then "max fan" (boost). Contains both the CS50's status telegram and the CI50's command telegrams. |
| `2026-08-14-tilluftkorrelasjon.hex.gz` | ~40 kB captured alongside readings from four Z-Wave duct sensors. Used to identify `0xC2` reg 0 slot 1 as supply air (B1), and to show that extract, exhaust and outdoor air are NOT present on the bus. |
| `2026-08-21-viftetrinn-ubalanse.hex.gz` | ~230 kB, 390 status telegrams, all of them showing the split-tap fault of [`PROTOCOL.md` §5.8](../../PROTOCOL.md): supply fan on tap 1, extract fan on tap 2, `[2]` = `0x89`, `[5]` = `0x12`, `[6]` bit0 clear. Taken 40 minutes into the fault, so it shows the steady state and not the transition into it. **One line per frame, not per 32 raw bytes** — this one was written from the component's frame logger, so bytes it rejected are absent. |

These are the basis for the frame structure and fan-level nibble findings in
[`../../PROTOCOL.md`](../../PROTOCOL.md).

Parse them like this:

```python
data = bytes(int(x,16) for x in open('...hex').read().split())

def cks(b):
    s1 = s2 = 0
    for x in b:
        s1 = (s1 + x) % 256
        s2 = (s2 + s1) % 256
    return s1, s2

frames, i = [], 0
while i < len(data) - 10:
    if data[i] != 0xC3:
        i += 1; continue
    ln = data[i+7]; end = i + 8 + ln
    if end + 2 > len(data): break
    if cks(data[i+5:end]) == (data[end], data[end+1]):
        frames.append(data[i:end+2]); i = end + 2
    else:
        i += 1
```

This yields 4500 frames with a valid checksum and zero false `C3` hits.

Contributions of captures from other installations are very welcome — see the
main README for what is most useful.
