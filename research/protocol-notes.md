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
   skinnen tåler (kildeimpedans er ikke målt) — se «Fysisk tilkobling» →
   «Strømforsyning» for last-testen og hva som er symptomet hvis den er for svak.
5. **Panel-adressering — mulig nøkkel til de ukjente headerbytene.** CI 50 har
   en dipswitch 3 som velger PANEL 1 / PANEL 2 (Flexits CI 50-manual: «Ved bruk
   av flere paneler må switch nr. 3 stilles på ulike verdier på hvert panel»).
   Bussen har altså adressering, og de faste bytene på indeks 0–4 i
   kommandotelegrammet — som vi i dag ikke vet betydningen av — kan godt kode
   panelidentitet. Hypotese å teste i Fase 1: avlytt CI50 med dipswitch 3 i
   begge stillinger og se hvilke byte som endrer seg. Det kan avklare flere
   ukjente felt på én test, og det er verdt å vite om vi kolliderer med det
   ekte panelet hvis vi injiserer som «panel 1» mens CI50 også er panel 1.
