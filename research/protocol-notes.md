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

## Flyttall-registre — UUTNYTTET (funnet 2026-08-13)

Rammetypene `0xC2` og `0xC7` fra CS50 (`sig 01 00 C4 4B`) bærer **IEEE754
float, little endian**, ikke byte-verdier. `payload[0]` er en bank (`0x20`
observert) og `payload[1]` er en **registerindeks som teller i registre, ikke i
byte** — den stepper 0, 7, 14, 21, fordi hver ramme bærer 7 floats (28 databyte).

Dekodet fra opptaket:

| Type | Reg | Verdier | Tolkning |
|------|-----|---------|----------|
| `0xC2` | 0 | `-55, 22.5…22.8, 0, 0, -55, 0, 0` | **levende temperaturmåling** i slot 1; `-55` er klassisk «føler ikke tilkoblet» |
| `0xC2` | 7 | `0, 15.0…25.0` | **settpunktet som float** — fulgte brukerens sykling 15→25→15 eksakt |
| `0xC7` | 0 | `0.01 ×4, 0.3 ×3` | regulatorparametere (P/I-ledd?) |
| `0xC7` | 7 | `0.3, 0.3, 0.3, 2, 1, 30, 25` | parametere/grenser |
| `0xC7` | 14 | `-20, -30, 2, 0, …` | grenseverdier, trolig temperaturgrenser |
| `0xC7` | 21 | `0, 0.1, 0.1` | parametere |

**Dette er den største uutnyttede muligheten i protokollen.** `0xC2` reg 0 slot 1
er en ekte, driftende temperatur — hvilken føler den tilhører (tilluft, avtrekk,
avkast, uteluft) er ikke bekreftet, men den kan eksponeres som `sensor` i HA så
snart den er identifisert. At settpunktet finnes både som byte (status
`payload[9]`) og som float (`0xC2` reg 7) gir en gratis kryssjekk.

At `0xC2` reg 7 slot 1 fulgte settpunktet er samtidig **uavhengig bekreftelse på
at hele bank/register-modellen er riktig** — vi forutså hvor verdien skulle
ligge, og den lå der.

## Temperaturfølere — identifisert ved korrelasjon (2026-08-14)

Metode: ferskt bussopptak samtidig med avlesning av fire uavhengige Z-Wave-
følere brukeren allerede har montert i kanalene (`sensor.friskluftanlegg_
sensorer_air_temperature_*`). De gir en fasit å måle bussverdiene mot.

| Referanse (HA) | Verdi |
|---|---|
| Tilluft | 19,06 °C |
| Avtrekk | 24,43 °C |
| Avkast | 22,43 °C |
| Uteluft | 16,06 °C |

Resultat:

| Register | Verdi | Konklusjon |
|---|---|---|
| `0xC2` reg 0 slot 1 | 19,7–19,9 | **TILLUFT** — avvik +0,64 °C mot HA-føleren |
| `0xC2` reg 0 slot 0 og 4 | `-55` | **føler ikke tilkoblet** (to ledige innganger) |
| `0xC2` reg 0 slot 2, 3, 5, 6 | `0` | ubrukt |
| `0xC2` reg 7 slot 1 | 15,0 | **settpunkt** — ikke en måling |

**Bussen har kun ÉN tilkoblet temperaturføler: tilluft.** Det stemmer med at
Flexit dokumenterer **B1 = «tilluftføler ettervarme»** på CS50 — føleren sitter
ved ettervarmebatteriet og er den regulatoren styrer etter. Avvikene på +0,64 °C
forklares naturlig av at B1 sitter rett ved batteriet, mens Z-Wave-proben sitter
lenger ute i kanalen.

De to `-55`-slottene er ledige følerinnganger på CS50-kortet. Avtrekk, avkast og
uteluft finnes **ikke** på bussen — de måles ikke av CS50 i vår konfigurasjon,
og brukerens Z-Wave-følere er derfor ikke overflødige.

### Falske treff — advarsel

En naiv «nærmeste verdi»-matching ga to feilkoblinger som er verdt å notere:
`0xC2` reg 7 slot 1 = 15 lignet på uteluft (16,06), men er settpunktet — bevist
ved at den fulgte brukerens sykling 15→25→15 eksakt dagen før. Og `0xC7` reg 7
slot 6 = 25 lignet på avtrekk (24,43), men er øvre settpunktgrense.
**Korrelér alltid mot en verdi som ENDRER seg, ikke mot et øyeblikksbilde.**

### `0xC7` er parametere, ikke målinger

Alle `0xC7`-verdier var identiske i to opptak et døgn fra hverandre:
`0.01 ×4`, `0.3 ×6`, `2`, `1`, `30`, `25`, `-20`, `-30`, `0.1 ×2`.
`15`/`25` og `-20`/`-30` ser ut som grenseverdier (settpunktspenn og
frostgrenser), og `0.01`/`0.3` som regulatorparametere.

## Skriving av viftetrinn og settpunkt — mekanismen (analysert 2026-08-14)

**Panelets tilstandsramme sendes KUN ved endring, ikke periodisk.** Det var den
avgjørende usikkerheten før Fase 2: ville CI50 overskrive en skriving fra oss
noen sekunder senere? Nei.

Målt på opptaket fra panelsekvensen:

| Mål | Resultat |
|---|---|
| Tilstandsrammer (`C1`/`len=8`) fra panelet | 21 |
| Av disse som ligger inntil en tilstandsendring | **21 av 21** |
| Lengste opphold mellom to slike rammer | 533 rammer |
| Observerte tilstandsendringer | 22 |

CS50 holder altså tilstanden selv, og panelet melder bare fra ved brukerinngrep.
Forseringen bekrefter det uavhengig: status sto på `0x31` i timevis mens
panelets siste melding sa `0x11` — CS50 speiler ikke panelet, den har egen
tilstand.

### Rammen

```
C3 04 00 C7 51 C1 04 08 | 20 0F 02 <vifte> <?> 04 00 <settpunkt> | CK CK
indeks                     8  9  10   11     12  13 14   15
```

- indeks 11 = viftetrinn, nibbel-kodet (`0x11`/`0x22`/`0x33`)
- indeks 15 = settpunkt, `0x0F`–`0x19` = 15–25 °C (verifisert over hele spennet)
- indeks 10 = `0x02` hos oss, `0x00` hos Vongraven — bruk VÅR verdi

### ADVARSEL: indeks 12 er trolig ikke forvarme

Koden vår skriver forvarme til indeks 12 (`data[4]`). Men i opptaket gikk det
feltet fra `00` til `01` i rammen som ble sendt **fem rammer før
forseringskommandoen** — altså ved trykk på «Max vifte», ikke ved noe som har
med forvarme å gjøre:

```
#2303  20 0F 02 11 01 04 00 0F   <- data[4] = 01
#2308  20 14 31 23               <- forseringskommandoen
```

Skriver vi forvarme dit, kan vi utløse noe helt annet enn vi tror.
**Hold forvarme utenfor første skriverunde**, og avklar feltet ved å slå
forvarmen av og på mens bussen logges.

## Arbitreringsfeilen — første sendeforsøk (2026-08-14)

Første trykk på forseringsknappen sendte **ingenting**, og satte i tillegg hele
mottaket ut av spill. Loggen:

```
[01:59:23][I] Køer forseringskommando (Max vifte)
[01:59:28][W] Ingen gyldige statustelegram siste 5000 ms
```

Null TX-linjer i `uart: debug: direction: BOTH`.

**Rotårsak — to feil som forsterket hverandre:**

1. **Off-by-one i header-hoppet.** Arbitreringen trigget på `C3` etterfulgt av
   `01`, og hoppet så over **seks** byte før den leste lengden. Men etter
   `C3 01` gjenstår bare fem headerbyte før `LEN` (`00 C4 4B TYPE b6`). Den
   spiste altså lengdebyten selv og leste første payload-byte — `0x20` = 32 —
   som lengde. 32+2 = 34 > 32 → «urimelig lengde» → ga opp.
2. **Ingen oppgivelse.** `command_pending_` ble aldri klarert, så mønsteret
   gjentok seg på **hver eneste** ramme. Siden praktisk talt alle CS50-rammer
   starter `C3 01`, ble sju byte spist ut av hver ramme, og ingen ramme kunne
   lenger parses. Mottaket var dermed permanent ødelagt til noden ble restartet.

**Fiksen: hele tilstandsmaskinen er fjernet.** Den var et levning fra før vi
hadde en rammeparser — den prøvde å finne slutten på et telegram ved å telle
byte manuelt. Rammeparseren *vet* allerede når et telegram slutter, og det er
nøyaktig det hullet vi skal sende i. Sendingen planlegges nå rett etter at en
ramme er ferdig validert:

```cpp
this->dispatch_frame_();
if (this->command_pending_) {
  this->set_timeout("flexit_sl4r_cmd", COMMAND_INJECT_DELAY_MS, [this]() { this->build_and_send_command_(); });
}
```

Det fjerner både off-by-one-en og muligheten for at en mislykket arbitrering
kan spise byte fra mottaket — det finnes ikke lenger en vei der byte går
utenom parseren.

**Lærdom:** å teste den tryggeste sendingen først var riktig. Feilen ville
oppstått like fullt ved skriving av viftetrinn, men da med en varig
tilstandsendring i spill i tillegg.

## Kollisjonen, og beviset på at vi ikke driver bussen (2026-08-14)

Etter at arbitreringen var fikset gikk rammen ut byte-perfekt:

```
>>> C3 04 00 C7 51 C1 04 04 20 14 31 23 51 B4
```

Identisk med CI50s egen forseringskommando, sjekksum og alt. **CS50 reagerte
likevel ikke** — verken ved kollisjon eller ved ren sending i et hull.

### Første forsøk avslørte hvorfor

Vi sendte midt i et pågående CS50-telegram:

```
[02:10:51.959] <<< C3 01 00 C4 4B
[02:10:51.981] >>> C3 04 00 C7 51 C1 04 04 20 14 31 23 51 B4
[02:10:52.014] <<< C6 01 16 20 1C 64 0F ...
```

Setter man de to RX-bolkene sammen får man `C3 01 00 C4 4B C6 01 16 20 1C …`,
altså en **uskadd ramme med gyldig sjekksum**. To sendere som driver samme
differensialpar samtidig ødelegger begge signalene. At CS50s telegram kom
gjennom uberørt beviser at **vår sender aldri nådde tråden**.

Mottak er altså bevist (vi leser bussen), sending er ikke.

### To kandidater

1. **`tx_pin: GPIO26` er feil eller ikke tilkoblet.** `rx_pin: GPIO32` er
   *bevist* riktig — vi mottar. TX-pinnen er kun antatt fra M5s pinout og aldri
   verifisert mot maskinvaren.
2. **Retningsstyringen slår ikke inn.** Konklusjonen om at Tail485 har
   automatisk retningsstyring uten DE/RE-linje er utledet fra blokkdiagrammet,
   ikke målt. Slår ikke DE inn, driver senderen aldri bussen.

### Decisiv test

Send en lang byte-burst mens bussen er travel. Blir CS50s telegrammer ødelagt
(sjekksumfeil i loggen), driver vi bussen og problemet er protokoll/timing.
Skjer det ingenting, er problemet fysisk — og da er TX-pinnen eller
retningsstyringen synderen.

### Sidefunn: ingen poll/svar-struktur

Hypotesen om at CI50 kun svarer på poll fra CS50 er avvist: panelets 912 rammer
følger etter alle mulige CS50-rammetyper (26 % `C6`/30, 20 % `C7`/30, resten
spredt). Panelet sender fritt når bussen er ledig — så en injeksjon fra oss
skal i prinsippet være like gyldig.

## Hva som er å hente fra Vongraven (gjennomgått 2026-08-14)

To ting, hvorav den ene endrer diagnosen.

### 1. Han sender hver kommando FEM ganger

```c
do { ...finn vindu...; Serial1.write(commandBuffer, 18); ++repeats; } while (repeats<5);
```

Grunnen er verdt å merke seg: **timingen hans var like gal som vår var.**
Lengdesjekken `if (3 < Length <33)` evalueres i C som `(3 < Length) < 33`, altså
`0 eller 1 < 33` — **alltid sann**, den gjør ingenting. Og han har samme
off-by-one i header-hoppet som vi hadde (leser `195`, `1`, hopper 6, leser så
første payload-byte som lengde).

Han kompenserte med gjentakelse. Brute force, ikke presisjon — og det forklarer
hvorfor det virket for ham. Implementert hos oss som `COMMAND_REPEATS = 5`,
hver sending i sitt eget målte stille vindu.

### 2. Han styrer retningen EKSPLISITT — og det gjør ikke vi

```c
digitalWrite(TXen, LOW);   // TX enable
delay(10);
Serial1.write(commandBuffer, 18);
Serial1.flush();
digitalWrite(TXen, HIGH);  // TX disable
```

Han hadde egne GPIO-er for `RXen`, `TXen` og til og med `COM_VCC` (MAX485
strømsatt kun ved behov). Han stolte aldri på automatikk.

**Det er den eneste arkitektoniske forskjellen som gjenstår.** Vår konklusjon om
at ATOM Tail485 har automatisk retningsstyring uten DE/RE-linje er utledet fra
M5s blokkdiagram — den er aldri målt.

### Konklusjon etter fem gjentakelser

Fem byte-perfekte sendinger i fem separate stille vinduer ga null reaksjon fra
CS50, og forstyrret heller ikke CS50s egen trafikk (`Kommunikasjon OK` holdt
seg på hele veien). Sammen med den tidligere kollisjonen som ikke skadet noe,
er konklusjonen at **senderen aldri driver differensialparet**.

Gjentakelse var altså riktig å implementere, men den kan ikke redde en sending
som ikke når tråden. Neste steg er fysisk: verifiser at ATOM-ens TX faktisk når
Tail485s TXD-inngang, og at DE/RE-automatikken i modulen fungerer.

## Driver vi bussen i det hele tatt? — designet test (2026-08-14)

### Først: en feilslutning som måtte korrigeres

Jeg konkluderte tidligere at vi ikke driver bussen, fordi en «kollisjon» ikke
skadet CS50s telegram. **Den slutningen var ugyldig.** `uart_debug` tømmer
RX-bufferet når retningen skifter — den logger ventende mottak FØR den logger en
sending. Bolken `C3 01 00 C4 4B` ble altså flushet *fordi* vi sendte, ikke fordi
CS50 var midt i en ramme. Det var aldri dokumentert noen kollisjon; jeg leste et
artefakt fra loggeren som fysikk.

### Den designede testen

Midlertidig bygg med `BUS_IDLE_BEFORE_TX_MS = 0` og `COMMAND_REPEATS = 30`:
send 30 rammer uten å vente på stille buss, altså med vilje oppå kontinuerlig
trafikk.

| Mål | Resultat |
|---|---|
| Sendinger avfyrt | 29 |
| Sjekksumfeil på mottatte rammer | **0** |
| Kommunikasjonsbrudd | **0** |

Hadde senderen vår drevet differensialparet, ville 29 blinde sendinger inn i
kontinuerlig trafikk uunngåelig ødelagt rammer. Ingenting skjedde.

**Konklusjon: senderen driver ikke bussen.** Mottak er bevist, sending er ikke —
og nå med et forsøk som er designet for å svare på spørsmålet, ikke tolket i
etterkant.

### Pinout er bekreftet riktig

M5s egen PinMap for Tail RS485 (SKU T002):

| ATOM Lite | Tail485 |
|---|---|
| G26 | TX |
| G32 | RX |
| 5V | 5V |
| GND | GND |

`tx_pin: GPIO26` / `rx_pin: GPIO32` er altså korrekt konfigurert. Feilen ligger
ikke i pinnevalget.

### Hva står igjen

Retningsstyringen. M5s dokumentasjon sier ingenting om DE/RE — verken at det er
automatikk eller at det finnes en enable-pinne. Antakelsen om automatikk kom fra
blokkdiagrammet i databladet, og den er aldri verifisert. Vongraven, som fikk
sending til å virke, styrte retningen eksplisitt.

Neste steg: mål A/B mot GND med multimeter under en sendeburst (30 gjentakelser
gir god tid), eller sett oscilloskop på paret. Er det ingen bevegelse, driver
ikke SP485-en, og da er det DE som ikke asserteres.

## Multimetermåling av sendeveien

Uten oscilloskop kan spørsmålet likevel avgjøres, fordi det egentlig er to
uavhengige spørsmål:

1. Sender ESP-en i det hele tatt ut på G26?
2. Kobler SP485-en senderen på differensialparet?

Slå på `switch.tx_test` (i HA, kategori diagnostikk) — den sender 16 nullbyte
hver 20 ms, altså ~40 % duty, så et multimeter rekker å sette seg. **Slå den av
igjen etterpå.**

### Måling 1 — sender ESP-en? (kan gjøres uten å demontere noe)

Mål **G26 mot G (GND)** på ATOM-ens header, DC-volt:

| TX-test | Forventet | Betydning |
|---|---|---|
| AV | ~3,3 V | UART-linja hviler høyt — normalt |
| PÅ | **merkbart lavere**, typisk 1,5–2,5 V | ESP-en sender ✔ |
| PÅ | fortsatt ~3,3 V | **ESP-en sender ikke** — feil pinne eller UART-problem |

Nullbyte er valgt nettopp fordi de gir mest tid i lav tilstand og dermed størst
utslag på gjennomsnittet.

### Måling 2 — driver SP485-en paret?

Denne bør gjøres **isolert fra Flexit-bussen**, ellers drukner målingen i CS50s
egen trafikk:

1. Trekk ut 4P4C-pluggen (da mister modulen bussforsyningen).
2. Mat ATOM-en via **USB-C** i stedet. Klemme V er nå frakoblet, så det er
   trygt — USB-C og klemme V må aldri være tilkoblet samtidig.
3. Mål **A mot B** på Tail485s klemmerekke, DC-volt.

| TX-test | Forventet | Betydning |
|---|---|---|
| AV | ~0 V | senderen er av (høyimpedans) — normalt |
| PÅ | **klart utslag**, flere hundre mV til et par volt | driveren kobler på ✔ |
| PÅ | fortsatt ~0 V | **DE asserteres aldri** — dette er feilen |

### Tolkning

- Måling 1 gir utslag, måling 2 ikke → retningsstyringen er synderen. Da må
  DE styres eksplisitt, slik Vongraven gjorde, eller modulen byttes.
- Måling 1 gir ikke utslag → problemet er før modulen: pinne eller UART.
- Begge gir utslag → vi driver faktisk bussen, og da er feilen i protokollen
  (adressering/rammeinnhold), ikke i maskinvaren.

## RETTELSE: vi driver bussen — maskinvaren er frisk (2026-08-14)

Konklusjonen om at senderen ikke nådde tråden var **feil**, og begge
begrunnelsene mine var det.

### Hvorfor stresstesten var ugyldig

Den lette etter «sjekksum feilet» i loggen. Men den meldingen kommer kun fra
`parse_and_publish_status_()`, altså for rammer som ALLEREDE har passert
rammeparserens sjekksum. Rammer som blir ødelagt forkastes **stille** i
parseren — helt bevisst, siden en `0xC3` inne i en payload treffer den grenen
normalt. Korrupsjon var dermed usynlig. Og siden vi bare forstyrret 29 rammer i
en kontinuerlig strøm, kom statusen tilbake innen sekundet, for kort til å
utløse timeout-advarselen. Testen kunne ikke oppdage det den lette etter.

### Målingen som avgjorde det

Brukerens multimeter viste at begge linjene løftet seg fra ~1,5 V til ~2 V når
TX-testen sto på, og falt tilbake etterpå — altså gjør modulen noe med paret.

Deretter ble `frames_discarded`-telleren lagt inn, som teller stille forkastede
rammer og gjør korrupsjon målbar:

| | |
|---|---|
| Forkastede rammer før test | **0** |
| Etter 20 s med kontinuerlig sending | **71** |
| «Kommunikasjon OK» | gikk **av** |
| Etter avslåing | på igjen, telleren stoppet |

**Vi driver differensialparet.** ESP-en sender, SP485-en kobler på, og
retningsstyringen i Tail485 fungerer som antatt. Maskinvaren er frisk.

### Konsekvens: feilen er i protokollen

Sendeveien er bevist ende til ende, så CS50 avviser oss på innhold eller
sekvens — ikke fordi rammen mangler.

Sterkeste spor fra opptaket: panelet sender **to** rammer ved et
forseringstrykk, ikke én.

```
#2303   20 0F 02 11 01 04 00 0F     <- tilstandsramme, data[4] går 00 -> 01
#2308   20 14 31 23                 <- forseringskommandoen
```

Vi har kun sendt den siste. Hypotese å teste: enten må de komme i par, eller så
er det tilstandsrammen med `data[4]=01` som faktisk utløser forseringen, mens
`20 14 31 23` er noe annet enn vi tror.

**Lærdom:** en negativ test er verdiløs uten en positiv kontroll. Telleren
skulle vært på plass før den første stresstesten ble tolket.

## Adressefeltet dekodet (2026-08-14)

De fire byte etter `0xC3` er ikke vilkårlige. **Byte 3–4 er Fletcher-sjekksummen
over `[0xC3, node, 0x00]`** — samme algoritme som resten av protokollen, brukt
på headeren:

| Node | Header | Beregnet |
|---|---|---|
| 1 (CS50) | `01 00 C4 4B` | ✓ |
| 4 (CI50, panel 1) | `04 00 C7 51` | ✓ |
| 5 (panel 2) | `05 00 C8 53` | ✓ (predikert, deretter sendt) |

**Byte 6 gjentar nodenummeret** — bekreftet på 4409 av 4500 rammer (98 %;
resten er parser-artefakter, se under). En ramme fra node N har altså N to
steder: i adressefeltet og på offset 6.

Implementert som `source_node:` i YAML-en, med adressen beregnet automatisk.

### Om «node 2, 3 og 5» i opptaket — parser-artefakter

Opptaket viste 10 rammer hver fra node 2, 3 og 5, alle med `TYPE=0xC3`,
`b6=01`, `LEN=0`. De er **falske positive**, ikke ekte noder:

For en nullengde-ramme er sjekksumvinduet `[0xC3, 0x01, 0x00]`, som alltid gir
`(0xC4, 0x4B)`. Treffer parseren en `0xC3` som ligger nøyaktig fem byte foran
starten på en ekte node-1-ramme, leser den `C3 01 00 C4 4B` som
`TYPE/b6/LEN/CK/CK` — og sjekksummen stemmer per konstruksjon. «Adressen» er da
bare de fire databytene som lå imellom.

Lærdom: en rammedetektor basert på lengde + sjekksum er ikke idiotsikker for
nullengde-rammer, fordi sjekksumvinduet da er så kort at det kan treffe tilfeldig.

## Sendeforsøk — status per 2026-08-14

Alle fire variantene nedenfor gikk beviselig ut på bussen (verifisert med
`direction: BOTH`, og `frames_discarded` steg ved kollisjon). **Ingen ga
reaksjon fra CS50.**

| Variant | Resultat |
|---|---|
| Node 4, kun `20 14 31 23`, ×1 | ingen reaksjon |
| Node 4, kun `20 14 31 23`, ×5 | ingen reaksjon |
| Node 4, tilstandsramme + kommando i par | ingen reaksjon |
| Node 5, par, med korrekt `b6=05` | ingen reaksjon |

### Gjenstående hypoteser, prioritert

1. **Enumerering ved oppstart.** CS50 registrerer trolig hvilke paneler som
   finnes når den starter. Et panel som dukker opp midt i drift blir kanskje
   aldri «godkjent». **Fang bussen under en strømsyklus av aggregatet** — det
   vil vise hele registreringssekvensen, og er det klart mest lovende neste
   steget.
2. **Panel 2 må konfigureres fysisk.** CS50 lytter kanskje bare til node 5 hvis
   et ekte panel med dipswitch 3 = ON har meldt seg. Jf. TODO punkt 5.
3. **`20 14 31 23` er ikke forsering.** Den er observert nøyaktig én gang, tolv
   rammer før statusendringen. Korrelasjon, ikke bevist årsak.
4. **Et felt vi ikke har identifisert** i tilstandsrammen må endres samtidig.

# GJENNOMBRUDD: bussen er POLLED (2026-08-14)

Alt over om «rammer» med et 8-byte hode var **feil modell**. Det jeg kalte én
ramme er i virkeligheten **to meldinger**: en poll fra masteren og et svar fra
den adresserte noden.

```
POLL  (fra master):   C3 <node> 00 <ck1> <ck2>              5 byte
                      ck = Fletcher over [C3, node, 00]

SVAR  (fra noden):    <TYPE> <node> <LEN> <data...> <ck1> <ck2>
                      ck = Fletcher over [TYPE, node, LEN, data...]
                      INGEN C3 — den tilhører pollen
```

Verifisert på 6144 byte fanget fra en strømsyklus: **194 svar, alle med gyldig
sjekksum, null ugyldige.**

### Enumerering ved oppstart

| Node | Poll | Svarer |
|---|---|---|
| 1 | 159 | ja (CS50s datastrøm) |
| 2 | 5 | nei |
| 3 | 5 | nei |
| 4 | 40 | ja (CI50, panel 1) |
| 5 | 5 | nei |

Node 2, 3 og 5 pollast **nøyaktig fem ganger, kun under oppstart**. Svarer de
ikke, droppes de resten av driftsperioden. Node 4 svarte og ble deretter pollet
kontinuerlig.

**Det var derfor alle sendeforsøk ble ignorert:** vi sendte uoppfordret på en
buss der ingen snakker uten å bli spurt. Og vi sendte med `C3`-hodet, altså
utga vi oss for å være masteren som poller — og svarte oss selv.

### Hvordan vi melder oss på

Svar på pollen til vår node. Da blir vi enumerert og pollet videre — også
gjennom våre egne omstarter (bekreftet: 91 svar etter en OTA-reboot).

Implementert som `source_node: 5` + `respond_to_polls: true`. Node 5 = panel 2,
altså den identiteten dipswitch 3 konfigurerer på et fysisk panel.

Mellom hendelser svarer vi det samme korte «ingenting å melde» som CI50 gjør:
`C0 <node> 02 22 00`.

## SKRIVING VIRKER — settpunkt (2026-08-14)

Første vellykkede styring av aggregatet fra Home Assistant.

Svaret på en poll er panelets tilstandsramme:

```
C1 <node> 08 | 20 0F 02 <vifte> <flagg> 04 00 <settpunkt> | ck ck
```

Satt fra HA: 15 → **18** → **21**. Bekreftet uavhengig ved at flyttall-
registeret `0xC2` reg 7 slot 1 — CS50s egen kringkasting av settpunktet — fulgte
etter hver gang. Det er ikke vår egen optimistiske UI-tilstand, det er
aggregatet som svarer.

### Viftetrinn virker IKKE ennå

Prøvd `0x22` (nibbel-par som i statusen) og `0x12` (forrige/nytt). Begge
ignoreres, mens settpunktet i samme ramme går gjennom. Observerte
kommandoverdier fra panelet: `0x32` ved overgangen 3→2, `0x21` ved 2→1, `0x11`
i ro — så kodingen er ikke åpenbar. Neste steg er å fange et viftetrinnskifte
gjort på panelet MENS vi er enumerert som node 5, og se nøyaktig hva som skiller
det fra vårt eget forsøk.
