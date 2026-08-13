# captures/ — rå bussopptak fra vårt eget anlegg

Fanget med `uart: debug:` i `flexit-atom-lite.yaml` (ligger kommentert ut der).
Formatet er én linje per 32 mottatte byte, hex, mellomromseparert — altså den
rå strømmen fra CS50/CI50, ikke ferdig oppdelte rammer.

| Fil | Innhold |
|-----|---------|
| `2026-08-13-panelsekvens.hex.gz` | ~140 kB buss-trafikk mens brukeren styrte panelet: ned til viftetrinn 1, hele settpunktspennet 17→25→15, deretter «Max vifte» (forsering). Inneholder både CS50s statustelegram og CI50s kommandotelegram. |
| `2026-08-14-tilluftkorrelasjon.hex.gz` | ~40 kB fanget samtidig med avlesning av fire Z-Wave-følere i kanalene. Brukt til å identifisere `0xC2` reg 0 slot 1 som tilluft (B1, ettervarmeføler) og til å vise at avtrekk/avkast/uteluft IKKE finnes på bussen. |

Grunnlaget for «Rammestruktur (målt)» og «Viftetrinn er to nibbler» i
[`../protocol-notes.md`](../protocol-notes.md).

Parse slik:

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

Ga 4500 rammer med gyldig sjekksum og null falske `C3`-treff.
