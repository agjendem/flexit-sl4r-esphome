# Flexit SL4R / CS50 RS485-protokoll — notater

Kilde: reverse-engineering av `Flexit_master.ino` (Vongraven-repoet) + verifisert
med Python mot README-eksemplene. Se `Flexit_master.ino` og `README.md` i denne
mappen for original-kilden.

## Buss-parametere
- UART 19200 baud, 8N1 (`Serial1.begin(19200, SERIAL_8N1)` i original)
- RS485 halv-dupleks, proprietær protokoll (IKKE Modbus)
- CS50 sender kontinuerlig 16 linjer med data i en løkke; kun linje 15 er
  interessant (status). CI50 (betjeningspanelet) sender kommandotelegrammer
  i "hullene" mellom CS50 sine linjer.

## Statustelegram (linje 15, sendt av CS50)

Byte-strøm (som observert på bussen), med offset relativt til en "sync-match":

```
... 195  h1 h2 h3 h4 h5  193  gap  22  [22 databyte]  ckA  ckB  ...
    ^i-8                  ^i-2  ^i-1  ^i (match-trigger)
```

Synkroniseringsregel (fra original-koden): se etter byte-verdi `22` der
byten 2 posisjoner tilbake er `193` OG byten 8 posisjoner tilbake er `195`.//
Byte'n `22` er telegramlengden (22 databytes følger).

Etter match leses **25 nye byte** rått inn i `rawData[0..24]`:

| Indeks (rawData) | Innhold                                          | Verifisert eksempel |
|-------------------|---------------------------------------------------|----------------------|
| 0                 | (ukjent, del av datablokk)                        | 32                   |
| 1                 | (ukjent)                                           | 14                   |
| 2                 | (ukjent)                                           | 145                  |
| 3                 | (ukjent)                                           | 128                  |
| 4                 | (ukjent)                                           | 0                    |
| **5**             | **Viftetrinn**: 17=trinn1, 34=trinn2, 51=trinn3    | 17                   |
| **6**             | **Forvarme på/av**: 0=av, 128=på                   | 0                    |
| 7                 | (ukjent)                                           | 4                    |
| 8                 | (ukjent)                                           | 0                    |
| **9**             | **Settpunkt varmeveksler** (°C, gyldig 15–25)      | 25                   |
| **10**            | Forvarme aktiv threshold1 (aktiv når verdi > 10)   | 0                    |
| **11**            | Forvarme aktiv threshold2 (inaktiv når verdi < 100)| 100                  |
| 12–21             | (ukjent, diverse driftsdata)                       | 0,49,49,0,0,0,152,136,136,0 |
| **22**            | **Sjekksum A**                                     | 179                  |
| **23**            | **Sjekksum B**                                     | 220                  |
| 24                | (ubrukt/neste telegram — ikke referert i original) | -                    |

### Sjekksumalgoritme (Fletcher-lignende, verifisert i Python)

```python
def checksum(data: bytes) -> tuple[int, int]:
    sum1 = sum2 = 0
    for b in data:
        sum1 = (sum1 + b) % 256      # egentlig ikke moddet underveis i original,
        sum2 = (sum2 + sum1) % 256   # men resultatet blir identisk siden vi
    return sum1 % 256, sum2 % 256    # kun bryr oss om verdien mod 256 til slutt
```

For statustelegrammet er sjekksum-vinduet **25 byte**, startende ved `193`-byten
(2 før sync-treff), IKKE ved `rawData[0]`:

```
input = [193, gap_byte, 22] + rawData[0..21]   # 3 + 22 = 25 byte
checksum(input) == (rawData[22], rawData[23])
```

Verifisert numerisk mot README sitt eksempel (linje 15-dump): stemmer eksakt
når `gap_byte = 1`. Denne byten er sannsynligvis en meldingstype/sekvens-ID og
bør IKKE hardkodes — komponenten fanger den faktiske verdien fra bussen ved
hver sync i stedet for å anta en konstant.

**MERK:** `gap_byte`s betydning er ikke bekreftet. Komponenten leser den
dynamisk fra strømmen (del av synk-vinduet), så dette er robust uansett om den
varierer.

## Kommandotelegram (sendt av CI50, 18 byte)

Eksempel fra README (verifisert sjekksum i Python):
```
195  4  0  199  81 | 193  4  8  32  15  0  34  0  4  0  18 | 52  236
 0   1  2   3    4  |  5   6  7   8   9 10  11 12 13 14 15 |  16   17
                       ^--- sjekksum-vindu [5..15] (11 byte) ---^   ^ckA ^ckB
```

- Indeks 0–4: fast header (adresse/protokoll-konstant), **antatt** lik på
  vårt anlegg — MÅ verifiseres ved avlytting i Fase 1 før noe sendes.
- Indeks 5: alltid `193` (samme byte som statustelegrammets sync-anker)
- Indeks 7: telegramlengde for kommandoen (8 = antall databyte inkl. seg selv)
- Indeks 11: **viftetrinn** (17/34/51 — samme koding som status)
- Indeks 12: **forvarme** (0=av, 128=på)
- Indeks 15: **settpunkt varmeveksler** (15–25)
- Indeks 16–17: sjekksum A/B, samme algoritme som status, men vindu = `[5..15]`
  (11 byte, dvs. FRA OG MED den faste 193-byten, EKSKLUSIV de 2 sjekksumbytene)

### Sending — bussarbitrering (fra original)

CI50 sender sine kommandoer i intervaller. For å ikke kollidere:
1. Lytt etter byte `195` etterfulgt av byte `1` (CI50s egen kommando-header-signatur)
2. Hopp over neste 6 byte
3. Les lengde-byte `L`, faktisk lengde å hoppe over = `L + 2`
4. Hopp over disse `L+2` byte (resten av CI50s eget kommandotelegram)
5. Vent ~10 ms, injiser deretter vårt eget 18-byte kommandotelegram

Original-koden gjør dette med blokkerende `while(!Serial1.available())`-løkker
på en Arduino Mega. **ESPHome sin `loop()` MÅ ikke blokkere** — komponenten her
er derfor skrevet som en ikke-blokkerende tilstandsmaskin drevet av
byte-for-byte-ankomst, med `set_timeout()` i stedet for `delay()` for
10 ms-pausen før sending.

## Usikkerheter som gjenstår (kun verifiserbart med maskinvare)

1. Om ATOM Tail485 (SP485EEN-L-basert) trenger eksplisitt DE/RE-styring via
   GPIO, eller om den auto-retningsstyrer (kun TX/RX/5V/GND er eksponert på
   baksidekontakten ifølge M5Stack-dokumentasjonen — ingen egen retningspinne
   er nevnt, noe som peker mot auto-retning). Komponenten støtter en valgfri
   `direction_pin` i konfigurasjonen i tilfelle det viser seg å trenges.
2. HELE 18-byte kommandomalen (alle indekser UNNTATT de 3 variable feltene
   11/12/15 og sjekksumbytene 16/17) er kopiert fra Vongravens eksempel og
   MÅ bekreftes/korrigeres ved avlytting av ekte CI50→CS50-kommandoer på
   vårt anlegg (Fase 1) — dette gjelder ikke bare "header" indeks 0–10, men
   også indeks 6, 8, 9, 10, 13 og 14 som i eksempelet har faste, ukjent
   betydede verdier (f.eks. indeks 13 = 4 i eksempelet).
3. `gap_byte`s eksakte betydning i statustelegrammet (mulig sekvens/type-ID).
4. Nøyaktig strøm-/spenningslevering på den ledige RJ10/RJ11-porten på CS50
   (må måles med multimeter før tilkobling av Tail485).
