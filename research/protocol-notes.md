# Flexit SL4R / CS50 RS485-protokoll — notater

Kilde: reverse-engineering av `Flexit_master.ino` (Vongraven-repoet) + verifisert
med Python mot eksemplene i hans README. Se `Flexit_master.ino` og
`vongraven-README.md` i denne mappen for original-kilden, og `README.md` for
hva som er kopiert kontra vår egen utledning.

## Buss-parametere
- UART 19200 baud, 8N1 (`Serial1.begin(19200, SERIAL_8N1)` i original)
- RS485 halv-dupleks, proprietær protokoll (IKKE Modbus)
- CS50 sender kontinuerlig 16 linjer med data i en løkke; kun linje 15 er
  interessant (status). CI50 (betjeningspanelet) sender kommandotelegrammer
  i "hullene" mellom CS50 sine linjer.

## Fysisk tilkobling

Kilde: Vongravens koblingsskjema (`images/vongraven-topology.png` i denne
mappa), M5Stacks datablad for Tail RS485 (SKU T002), Flexits egne manualer for
CI 50 og CI 66, og pinout-dokumentasjonen for søstergenerasjonen CS60 i
[patstave/Node-FlexitCS60-RS485](https://github.com/patstave/Node-FlexitCS60-RS485).

### Kontakttype

CS50 bruker **4P4C** (den 4-polede telefonrør-kontakten, uformelt kalt RJ10 /
RJ22 / RJ9 / RJH) — **ikke** RJ11/RJ12, som er 6-posisjons. Tidligere versjoner
av disse notatene sa «RJ10/RJ11»; RJ11 er direkte feil og fører til feil
kabelkjøp. Søstergenerasjonen CS60/CI60 bruker derimot 6-polet RJ12.

Kortet har to like kontakter (CI50 i den ene, ledig i den andre). CI50-panelet
har i tillegg en ledig kontakt på baksiden — Flexits CI 50-manual: «Ledningen
klikkes i hvilken som helst av de 2 kontaktene bak på styrepanelet … Det er
mulig å koble opp til 2 styrepanel til hvert aggregat.» Elektrisk samme buss,
og panelet er som regel lettere å komme til enn innmaten i aggregatet.

**Valgt tilkoblingspunkt: den ledige kontakten bak på CI50-panelet.**

RS485 er en **multi-drop-buss**: de to kontaktene er parallellkoblet — bare
loddet sammen på samme fire ledere — ikke seriekoblet. Signalet går ikke
*gjennom* panelet og videre ut; det er ett felles bussegment som alle noder
henger på. Topologisk ser det ut som en kjede, elektrisk er det en stjerne av
korte stubber på samme par. Det er nettopp derfor Flexit kan la deg henge på et
panel nr. 2 (jf. dipswitch 3 = PANEL 1/2).

Konsekvensen: en node i den ledige panelkontakten ser nøyaktig samme buss som
en node på CS50-kortet inne i aggregatet. Ingenting tapt på å slippe å åpne
aggregatet. Målingen bekrefter det empirisk — 11,8 V er til stede på
panelkontakten, altså føres GND og +V helt fram, og at CI50 fungerer i den
andre kontakten viser at A/B også er der.

Terminering skal ikke legges til: 19200 baud over noen titalls meter har
stigetider som er lange nok til at refleksjoner er uinteressante, og bussen har
allerede det den trenger.

### Pinout (4P4C, standard fargekode)

| Pinne | Farge | Funksjon | Tail485-klemme |
|-------|-------|----------|----------------|
| 1 | svart | GND / signalreferanse | **G** |
| 2 | rød   | **B** (D1) | **B** |
| 3 | grønn | **A** (D0) | **A** |
| 4 | gul   | **+V, målt 11,8 V** — se «Strømforsyning» | **V** |

Pinne 1–3 er verifisert to uavhengige veier: bildeanalyse av Vongravens
topologiskjema (svart→GND, grønn→A, rød→B på MAX485-modulen) og hans egen
beskrivelse i hjemmeautomasjon.no-tråden om CI60.

Pinne 4 er **målt 2026-08-13: 11,8 V** mot pinne 1, i enden av den ~12 m lange
tilførselsledningen til CI50-panelet. Det bekrefter tolkningen som var utledet
strukturelt her tidligere (CI50 er et rent veggpanel uten annen tilførsel, så
bussen *må* mate det; CS60/RJ12 har samme oppbygning med datapar i midten og
GND/forsyning på ytterkantene, pinne 1–2 GND, 3 A+, 4 B−, 5–6 **+12 V**).
Vongraven lot pinne 4 stå ubrukt og matet MAX485 fra Arduinoens 5 V, så dette
er ikke dokumentert noe annet sted enn her.

11,8 V på 12 m kabel betyr også at spenningsfallet i tilførselen er neglisjerbart
ved tomgang — men det sier ingenting om hvor mye strøm skinnen tåler, se under.

**A/B-merkingen spriker mellom kilder** (M5s klemmemerking, MAX485-moduler,
Flexits egen konvensjon). Er bussen helt stille ved første forsøk: bytt om A og
B. Det er ufarlig og tar ti sekunder — gjør det før du mistenker koden.

### Strømforsyning

ATOM Tail485 har en AOZ1282CI-buck som tar **9–24 V** på klemme V/G og mater
ATOM-ens 5 V-skinne. M5s egen produktbeskrivelse: «can directly convert the 12V
voltage of RS485 to 5V to power the Type-C interface, eliminating the need for
separate power supply.» Modulen er altså laget for akkurat dette bruksmønsteret.

**AVKLART ved måling 2026-08-13: 11,8 V.** Godt innenfor 9–24 V, med ~2,8 V
margin ned til nedre grense. Hele oppsettet mates fra bussen — alle fire
klemmene (B, A, V, G) i bruk, ingen egen strømforsyning, én kabel.

Det som **ikke** er avklart er hvor mye strøm skinnen tåler. Tomgangsspenning
sier ingenting om kildeimpedansen. CI50 selv er noen lysdioder og brytere, så
skinnen er ikke nødvendigvis dimensjonert for en ESP32 med WiFi (100–200 mA
snitt på 5 V-siden, topper mot 500 mA ved sending → ca. 50–100 mA fra 12 V etter
bucken, mer i toppene). Last-test før du stoler på den: heng på en motstand som
trekker omtrent det samme (100 Ω over 12 V ≈ 120 mA) og se om spenningen synker.
Faller den under ~9 V under last, kommer ESP-en i boot-loop — og et sagende
buss-nivå kan i verste fall forstyrre CI50/CS50, altså ventilasjonen, ikke bare
ESP-en.

Uten motstand for hånd: koble til, og hold øye med to ting i loggen — brownout-
reset fra ESP32 og at CI50-panelet fortsatt oppfører seg normalt. Ved tvil, mat
fra USB-C i stedet (se under).

**Polaritet:** kontroller med multimeter hvilken leder som er + rett før du
plugger inn — buck-omformere av denne typen har normalt ingen
reverspolaritetsbeskyttelse, så V og G byttet om kan ødelegge Tail485-modulen.
Fargekoden i tabellen over gjelder standard 4P4C-kabel, men modulkabler finnes i
både rett og speilvendt (telefonrør-)utførelse, hvor pinne 1↔4 og 2↔3 er byttet
om i den ene enden. Stol på måleren, ikke på fargen.

**Ikke ha USB-C og klemme-V tilkoblet samtidig** — DC/DC-utgangen mater rett inn
på samme node som USB 5V-IN. Ved flashing over USB: koble fra V (eller hele
4P4C-pluggen).

### Pinne-mapping ATOM Lite ↔ Tail485

M5s PinMap: **G26 = TX, G32 = RX**, 5V, GND. Dette er det `flexit-atom-lite.yaml`
allerede bruker. Kun disse fire signalene går mellom modulen og ATOM-en — det er
ingen DE/RE-linje, se usikkerhet 1 under.

## Rammestruktur (MÅLT på eget anlegg 2026-08-13)

Avlyttet 23 708 byte fra bussen med `uart: debug:` og analysert i Python.
Dette er ikke lenger utledning — det er målt.

```
C3  b1 b2 b3 b4  TYPE  b6  LEN  [LEN databyte]  CK1 CK2
^0  ^1 ^2 ^3 ^4  ^5    ^6  ^7   ^8 ...
                 └──── sjekksumvindu: [5 .. 8+LEN) ────┘
```

**766 rammer parset, 0 forkastede.** Hver eneste `C3` i strømmen var starten
på en ramme med korrekt sjekksum — ingen falske positive over 23 kB. Det gjør
lengde+sjekksum til en trygg rammedetektor.

- `TYPE` (offset 5) sett med verdiene `0xC0`–`0xC7`. `0xC1` med `LEN`=22 er
  statustelegrammet vårt.
- Minst to avsendere, skilt av byte 1–4: `C3 01 00 C4 4B …` og
  `C3 04 00 C7 51 …`. Sannsynligvis CS50 og CI50 — se usikkerhet 5 om
  panel-adressering, som nå kan testes direkte mot disse bytene.
- Sjekksumalgoritmen fra Vongraven **stemmer eksakt** (Fletcher-lignende,
  se under), med vindu fra offset 5 til og med siste databyte.

### Off-by-one i synkroniseringsregelen — RETTET

Vongravens notat sa: byte `22` der 2 tilbake er `193` og **8 tilbake** er `195`.
På vårt anlegg ligger `195` **7 byte** foran lengdebyten, fordi headeren er
8 byte og `LEN` står på offset 7.

Målt over de samme 23 708 bytene:

| Regel | Treff |
|-------|-------|
| `195` ved i−8 (Vongravens/vår opprinnelige) | **0** |
| `195` ved i−7 (faktisk) | **41** |

Det var hele grunnen til at Fase 1 var stum: bussen var riktig koblet og
telegrammene kom inn hele tiden, men synkroniseringen traff aldri. Rettet i
`handle_incoming_byte_()` (`sync_history_[1]` i stedet for `[0]`).

**Payload-layouten fra Vongraven stemmer derimot uendret.** Første verifiserte
statustelegram fra vårt anlegg:

```
header : C3 01 00 C4 4B C1 01 16
payload: 20 0E 24 80 02 33 00 04 00 11 00 00 00 64 64 20 00 00 98 88 88 00
         [5]=0x33=51 → viftetrinn 3
         [6]=0x00    → forvarme av
         [9]=0x11=17 → settpunkt varmeveksler 17 °C
```

Bekreftet mot HA etter fiksen: `Viftetrinn` = 3, `Settpunkt varmeveksler` =
17,0 °C, `Forvarme aktiv` = av, `Kommunikasjon OK` = på.

## Viftetrinn er to nibbler (MÅLT 2026-08-13)

Vongraven beskrev `payload[5]` som 17/34/51 = trinn 1/2/3, altså `verdi/17`.
Det er en tilfeldighet som holder for tre av verdiene. Ved å avlytte et styrt
eksperiment (ned til trinn 1, hele temperaturspennet opp og ned, deretter
«Max vifte») dukket det opp verdier den modellen ikke dekker:

| Rå | Hex | Høy nibbel | Lav nibbel | Vifte-% (`p13`/`p14`) | Betydning |
|----|-----|-----------|-----------|----------------------|-----------|
| 17 | 0x11 | 1 | 1 | 49 | trinn 1 |
| 34 | 0x22 | 2 | 2 | 74 | trinn 2 |
| 51 | 0x33 | 3 | 3 | 100 | trinn 3 |
| 49 | 0x31 | 3 | 1 | 100 | **forsering**: kjører 3, faller tilbake til 1 |
| 33 | 0x21 | 2 | 1 | 74 | overgang |

**Høy nibbel = trinnet aggregatet kjører på. Lav nibbel = trinnet det
returnerer til.** Det stemmer med CI 50-manualen, som sier at et blinkende lys
under forsering viser hastigheten aggregatet hadde før forseringen, og at
aggregatet står på trinn 3 mens den varer.

Uavhengig bekreftelse: `payload[13]` og `payload[14]` er viftepådrag i prosent
for de to viftene, og de følger **høy** nibbel (49/74/100 %), ikke `verdi/17`.

Konsekvens for koden: den gamle vakten godtok kun 17/34/51 og avviste dermed
forsering helt, så entiteten frøs på forrige trinn — og `49/17 = 2` ville gitt
feil trinn om den hadde sluppet gjennom. Rettet til `raw >> 4`.

**Åpen mulighet:** lav nibbel ≠ høy nibbel er en presis forserings-indikator.
Verdt å eksponere som egen `binary_sensor` («Forsering aktiv») senere.

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

1. ~~Om ATOM Tail485 trenger eksplisitt DE/RE-styring via GPIO.~~
   **AVKLART (dokumentasjon):** blokkdiagrammet i M5Stacks datablad for Tail
   RS485 (SKU T002) viser at kun fire signaler går mellom modulen og ATOM-en —
   G, 5V, TXD, RXD. Det finnes ingen DE/RE-linje ut av modulen, så SP485EEN-L
   må retningsstyres av kretsen på kortet. La `flow_control_pin` stå
   ukonfigurert. (Komponenten støtter den valgfritt — merk at konfignøkkelen
   heter `flow_control_pin`, ikke `direction_pin` som disse notatene tidligere
   påsto.) Bekreft med maskinvare at sending faktisk kommer ut på bussen i
   Fase 2, men det er ikke lenger et åpent designspørsmål.
2. HELE 18-byte kommandomalen (alle indekser UNNTATT de 3 variable feltene
   11/12/15 og sjekksumbytene 16/17) er kopiert fra Vongravens eksempel og
   MÅ bekreftes/korrigeres ved avlytting av ekte CI50→CS50-kommandoer på
   vårt anlegg (Fase 1) — dette gjelder ikke bare "header" indeks 0–10, men
   også indeks 6, 8, 9, 10, 13 og 14 som i eksempelet har faste, ukjent
   betydede verdier (f.eks. indeks 13 = 4 i eksempelet).
3. `gap_byte`s eksakte betydning i statustelegrammet (mulig sekvens/type-ID).
4. ~~Faktisk spenning på pinne 4 (4P4C).~~ **AVKLART (målt 2026-08-13): 11,8 V**
   på CI50-panelets ledige kontakt, i enden av 12 m tilførsel. Innenfor
   Tail485s 9–24 V → oppsettet mates fra bussen. Gjenstår: hvor mye **strøm**
   skinnen tåler. **Delvis avklart 2026-08-13:** noden har kjørt på
   bussforsyning gjennom flere OTA-runder uten problemer, og `Resetårsak`
   rapporterer «Reboot request from esphome.ota» — ingen brownout. Et kort
   blink på styreenheten i innpluggingsøyeblikket var innkoblingsstrøm fra
   Tail485s inngangskondensator, ikke en sagende skinne. Sensorene
   `Resetårsak` og `Oppetid` står permanent i konfigen og fanger opp en
   eventuell svikt under vedvarende last.
5. **Panel-adressering — mulig nøkkel til de ukjente headerbytene.** CI 50 har
   en dipswitch 3 som velger PANEL 1 / PANEL 2 (Flexits CI 50-manual: «Ved bruk
   av flere paneler må switch nr. 3 stilles på ulike verdier på hvert panel»).
   Bussen har altså adressering, og de faste bytene på indeks 0–4 i
   kommandotelegrammet — som vi i dag ikke vet betydningen av — kan godt kode
   panelidentitet. **Målt 2026-08-13:** bussen har fem avsendersignaturer i
   byte 1–4 — `01 00 C4 4B` (x3558), `04 00 C7 51` (x912), og tre sjeldne:
   `02 00 C5 4D`, `03 00 C6 4F`, `05 00 C8 53` (x10 hver). Byte 1 ser ut som en
   node-ID, og byte 3–4 følger den. Fabrikkinnstillingen er dipswitch 3 = OFF
   = PANEL 1 (bekreftet i manualens figur), så panelet vårt er panel 1.
   Hypotese å teste i Fase 1: avlytt CI50 med dipswitch 3 i
   begge stillinger og se hvilke byte som endrer seg. Det kan avklare flere
   ukjente felt på én test, og det er verdt å vite om vi kolliderer med det
   ekte panelet hvis vi injiserer som «panel 1» mens CI50 også er panel 1.

   **Må vi gjøre noe med dip 3 nå? Nei.** Dipswitchen sitter på et fysisk
   CI50-panel, og vi legger ikke til et panel nr. 2 — vi henger en lyttende
   node på bussen. I Fase 1 sender vi ingenting og trenger derfor ingen
   adresse; det eksisterende panelet skal stå urørt som PANEL 1.

   Det blir først relevant i **Fase 2**, hvor vi injiserer telegrammer med
   header-byte vi ikke kjenner betydningen av. Rekkefølgen bør være:

   1. Avlytt i Fase 1 med panelets dip 3 i stilling 1, så i stilling 2, og
      diff headeren. **Sett den tilbake til utgangsstillingen etterpå.**
   2. Endrer ingen byte seg: identiteten ligger ikke i headeren, og vi kan
      injisere med malen som den er. (Vongraven gjorde nettopp det, ved siden
      av et levende CI50, uten rapporterte problemer — indikasjon, ikke bevis.)
   3. Endrer noe seg: sett vår injeksjon til den **motsatte** panel-ID-en av
      det ekte panelet. CS50 støtter to paneler fra før, så vi opptrer da som
      et legitimt panel 2 i stedet for å krangle med CI50 om samme adresse.

   Uansett utfall er bussarbitreringen (sende i hullet etter CI50s eget
   telegram) viktigere for å unngå kollisjon enn adressen er.

## Forseringskommandoen (MÅLT og verifisert 2026-08-13)

Fanget da brukeren trykket «Max vifte» på CI50 mens bussen ble avlyttet.
Komplett ramme, sjekksum verifisert i Python:

```
C3 04 00 C7 51 C1 04 04 20 14 31 23 51 B4
^  ^--sig--^  ^t ^b6 ^len ^--data--^ ^ck^
```

Den er en **engangs-kommando**, ikke en tilstandsskriving: den forekom nøyaktig
én gang i 4500 rammer, tolv rammer før statusen slo om fra `0x11` til `0x31`.
Aggregatet faller selv tilbake til forrige trinn når perioden er over, så det
finnes ingen «av»-kommando å sende — og vi fanget da heller ingen, siden
opptaket sluttet mens forseringen fortsatt løp.

Implementert som `button` («Forsering») via `trigger_boost()`, som går utenom
`command_template`-modellen fordi rammen ikke har felt som må speiles fra
gjeldende tilstand.

## command_template ER VERIFISERT (2026-08-13)

Panelet sender periodisk sin ønskede tilstand som en `C1`/`len=8`-ramme. Den
har nøyaktig samme form som malen vi har hatt liggende ubekreftet:

| | sig | type | b6 | len | data |
|---|---|---|---|---|---|
| Vongravens mal | `04 00 C7 51` | C1 | 04 | 08 | `20 0F 00 22 00 04 00 12` |
| Målt hos oss | `04 00 C7 51` | C1 | 04 | 08 | `20 0F 02 11 00 04 00 12` |

- `data[3]` = viftetrinn (samme nibbel-koding som status: `0x11`/`0x22`/`0x32`)
- `data[7]` = settpunkt varmeveksler — observert telle `0F`…`19` (15–25 °C) i
  takt med at brukeren syklet hele spennet på panelet
- `data[2]` er det ENESTE avviket: `02` hos oss mot `00` hos Vongraven.
  Betydning ukjent — sannsynligvis en modell-/konfigurasjonsforskjell.

Full-ramme-indeksene i Vongravens notat stemmer dermed også: indeks 11 =
viftetrinn (`8+3`), indeks 15 = settpunkt (`8+7`).

**Gjenstår før tilstandsskriving aktiveres:** avklare `data[2]`, og teste at
injeksjon ved siden av et levende CI50 ikke gir konflikt — panelet sender jo
sin egen tilstand periodisk og vil kunne overskrive vår skriving igjen.
