# TODO — hva som gjenstår

Status per 14. august 2026: **toveis styring virker.** Lesing og skriving av
viftetrinn, settpunkt og forsering er verifisert mot CS50s egne verdier. Det som
står igjen er kartlegging av felt vi ennå ikke har tydet, og noen få
robusthetspunkter.

Utledningen ligger i [`research/protocol-notes.md`](research/protocol-notes.md),
råopptakene i [`research/captures/`](research/captures/).

**Arbeidsmåten som har fungert:** slå på `uart: debug:` i
`flexit-atom-lite.yaml`, OTA, fang loggen til fil, parse med
poll/svar-validatoren, og *diff mot en kjent tilstand*. Alle funn kom fra å
endre én ting av gangen og se hva som fulgte. Skru av debug etterpå.

**Metodelærdom verdt å ta med:** en negativ test er verdiløs uten positiv
kontroll, og en plausibel hypotese fortjener en måling før den blir til en fiks.
To ganger denne uka pekte «åpenbare» forklaringer feil vei, og begge gangene var
det en billig måling som avslørte det.

---

## Neste — de mest verdifulle

### 1. Kartlegg de ukjente statusfeltene mot recorder-historikk

Feltene ligger allerede eksponert som diagnostikk i HA (`raw_status_bytes`), så
historikken bygger seg opp uten at du trenger å gjøre noe. Etter noen dager kan
hypoteser prøves mot uker med data i stedet for et nytt bussopptak.

- [x] ~~`[2]` bit0~~ **= varmegjenvinneren går** (settes når pådraget passerer
      ~10, altså når rotoren faktisk snurrer). Eksponert som entitet.
- [x] ~~`[2]` øvrige bit~~ **= viftenes relétilbakemelding.** Bit 2–4 og 5–7 er
      to one-hot-grupper (trinn 3/2/1) for hver sin vifte, verifisert mot 592
      telegram med null bom. Kun **bit 1** står igjen som ukjent — aldri
      observert satt i 837 telegram. Hypotese: gruppa bit 0–1 hører til
      gjenvinneren, og bit 1 koder trolig **bypass** på plateveksleraggregater
      (Flexit bruker samme utgang til «rotor eller bypass motor»). Kan bare
      avgjøres av noen med plateveksler.
- [x] ~~`[11]`: `0/1`~~ **= varmepådraget** (se punkt 3 under).
- [x] ~~`[15]`: `32/35`, `[20]`: `68/136` — veksler disse i takt med `[6]`?~~
      **JA — og alle tre flipper ved FORSERINGSSTART** (fase 0-analysen
      2026-08-15). Blinkfase-hypotesen er avvist: én overgang i hele opptaket,
      i nøyaktig samme telegram som boost. Betydning fortsatt ukjent
      (`[20]` er et rent nibbelskift `0x88`→`0x44`), men korrelatet er kjent.
- [ ] **`[6]` bit0 — RE-VERIFISER: forsering eller element?** Beviset for
      «elementet varmer» røk i fase 0-analysen: eneste bit0=1-observasjon er
      forseringsperioden, og ettervarmen var da *deaktivert*. Test live:
      utløs forsering med ettervarme av (flipper bit0 → forsering), og se
      etter bit0=1 uten forsering en kald morgen (→ element). Entiteten
      «Ettervarme aktiv» er feilmerket inntil dette er avgjort.
- [ ] `[12]`, `[16]`–`[19]`, `[21]`: ingen variasjon observert ennå

Korrelér mot noe som endrer seg — utetemperatur, viftetrinn, tid på døgnet.
Bekreftet fra før: `[5]` viftetrinn, `[9]` settpunkt, `[13]`/`[14]` viftepådrag.

### 2. Alarmbitfeltet — filteralarmen er løst, resten gjenstår

Filteralarmen var det punktet med størst praktisk nytte, og den er i mål.
Mekanismen er tidsbasert («filtertid»); CS 50 har ingen trykkvakter.

- [x] ~~Finn alarmbyten.~~ **`payload[4]` bit 1.** Fanget da brukeren
      nullstilte alarmen ved et uhell under ettervarme-forsøket: `2` → `0`.
      Eksponert som `binary_sensor` **«Filteralarm»**.
      Retroaktiv forklaring: `[4]` sto konstant `2` hos oss og `0` hos
      Vongraven — hans filteralarm var bare ikke aktiv.
- [ ] **Kartlegg de øvrige bitene i `[4]`.** Trolig et alarmbitfelt.
      **Rotoralarm** og **overhetingstermostat** er de nærliggende kandidatene,
      og begge er dokumenterte CS 50-overvåkingsfunksjoner. Overhetingsalarmen
      er sikkerhetsrelevant.
- [x] ~~Fang reset-kommandoen.~~ **GJORT** — den lå allerede i opptaket fra
      ettervarme-forsøket: `data[4] = 0xC0` i en ellers vanlig tilstandsramme.
      Feltet rapporterer knappehendelser (`0x01` = forsering, `0xC0` = begge
      temperaturknapper). Implementert som knappen **«Nullstill filtervakt»**,
      som kjører hele manualens prosedyre automatisk: settpunkt til 20, reset,
      og tilbake til opprinnelig settpunkt.
- [ ] **Verifiser at timeren faktisk restarter — NY METODE (fase 0, 2026-08-15):**
      les filtertelleren (`C6 20 1C` ord 8) før og etter «Nullstill filtervakt».
      En ekte nullstilling skal sette telleren til 0; da trengs ingen ukers
      venting. NB: 14. august-hendelsen nullstilte IKKE telleren (den fortsatte
      29351 → 29364) — det er derfor alarmen re-armerte.
- [x] ~~Sjekk om **filtertiden/tidstelleren** også ligger på bussen.~~
      **FUNNET (fase 0, 2026-08-15):** to timetellere som tikker 1/time —
      `C6 20 0E` ord 10 (33500+, total driftstid/«Tidsteller») og `C6 20 1C`
      ord 8 (29342+, timer siden filternullstilling/«Filtertid»). Manualens
      filterintervall er 0–12 mnd (std 6 — og `00 06`=6 ligger i reg `0x0E`).
      Eksponeres som sensorer i fase 2; «dager til filterbytte» kan beregnes.

### 3. ~~Rotorpådrag~~ — FUNNET 2026-08-15

`payload[11]` er varmepådraget som regulerer rotoren. Sto konstant `0` i alle
opptak til settpunktet ble satt til maks; da rampet den monotont `0 → 68`.
Eksponert som **«Varmepådrag»**. Trengte ikke høsten — bare et provosert
varmebehov.

Se «GJENSTÅR Å DEKODE» i protokollnotatene for de fem hullene som er igjen.

### 3b. Ettervarmens eget pådrag — fyringssesongen

CS 50-manualens klemmeliste bekrefter at begge finnes, og ingen av dem er
merket «ikke CS 50»:

| Klemme | Funksjon |
|---|---|
| J5 (Pin 11,12) | Styresignal til gjenvinner (rotor), 0–10 V |
| J5 (Pin 9,10) | Styresignal til ettervarme, 0–10 V |
| J5 (Pin 13,14) | Rotoralarm |

- [ ] Endre settpunktet når rotoren faktisk må jobbe, og se hvilken verdi som
      følger med. Nå i august er differansen ute/inne for liten til å skille et
      pådrag fra en konstant — **høstforsøk**.
- [ ] Let etter **ettervarmens** 0–10 V-pådrag på samme måte. Se hypotesen om
      `payload[6]` under.
- [ ] **Rotoralarm** skal finnes som statusbit.
- [ ] **Overhetningstermostat (OT) — sikkerhetsrelevant, prioriter denne.**
      Aggregatet har ettermontert elektrisk ettervarme (sett 94283-01), og OT-en
      er en alarmkilde CS 50 overvåker («Elektrisk batteri, termostat»). Løser
      den ut, er elementet overopphetet og må resettes manuelt inne i
      aggregatet. Et varsel i HA er verdt mer enn de fleste andre feltene.
      Kan ikke fremprovoseres trygt — men finn kandidatbyten, så den fanges
      hvis det først skjer.

## Feature-frame: kan vi be om utstyrskonfigurasjonen?

Målet er at integrasjonen skal kunne slå entiteter av og på selv, ut fra hva
aggregatet faktisk har.

- [x] ~~Er `C0` en leseforespørsel?~~ **NEI — testet og avvist.** Av 27 `C0`-rammer
      (bank+reg, ingen data) ble **0** etterfulgt av svar med samme bank/reg.
      Vi har dermed **ingen kjent måte å be om et bestemt register på**.
- [x] ~~Let i `0xC6`-parameterblokkene.~~ **DELVIS DEKODET 2026-08-15.**
      `0xC6` og `0xC7` er **manualens parametertabeller**, men **ikke** i
      menyrekkefølge — de er gruppert etter type (min/maks-par ligger sammen).
      Matching mot **hele** CS 500-lista (ikke bare CS-50-delmengden) gjenfant
      36 av 38 standardverdier, og 17 av 37 av manualens nabopar ligger som
      nabobytes — bl.a. kjeden `16,35 / 16,35 / 15,2`. **Bekreftet at layouten
      er CS 500 sin:** kjøleparameterne `45` og `180` («ikke CS 50») ligger i
      registrene selv om kortet vårt ikke har kjøling. Se «Kan plasseringene
      utledes fra manualens rekkefølge?» i protokollnotatene.
- [ ] **Finn utstyrskonfigurasjonen blant restene.** Manualens seksjon **4.91
      «Komponenter»** er nettopp utstyrskonfigurasjonen, og den er én av de 13
      som gjelder CS 50. Den ligger mellom 4.84 (motorvern-forsinkelse, funnet
      som `00 1E`=30) og 4.92 (versjonsstrengen, funnet). Er registerrekkefølgen
      nær menyrekkefølgen, bør konfigurasjonen ligge kort etter motorvern-
      verdien i reg `0x0E` — nærmeste kandidater er `02 32` og `0F 01`.
      **NB:** registrene følger ikke menyrekkefølgen (se over), så «kort etter»
      er en svak føring. Klyngestrukturen hjelper derimot: konfigurasjonen er
      ikke et min/maks-par, så den skal ligge utenfor de identifiserte klyngene.
      **Fase 0-oppdatering (2026-08-15):** `0F 01` er UTE av jakten — det er de
      lagrede brukerinnstillingene (settpunkt + viftetrinn, fulgte
      panelsekvensen slavisk i opptaket). `82 xx` er driftstimetelleren.
      Gjenværende kandidat i reg `0x0E`: `02 32`, pluss `05 0C` i reg `0x00`.
      Avgjøres sikkert bare ved å diffe mot et anlegg med annen utrustning.
- [ ] **Sammenlign med et annet anlegg.** Uten en fasit å diffe mot er det
      gjetting. To anlegg med ulik utrustning ville avslørt feltene direkte.
- [ ] **Finnes det en skrivevei til parameterregistrene?** Vi kan skrive
      panelets tilstandsramme (bank `0x20` reg `0x0F`). Om samme mekanisme
      virker mot andre registre er uprøvd — og potensielt risikabelt, siden
      `0xC6`-blokkene inneholder driftsparametere.

## For publisering

### Automatisk maskinvaregjenkjenning — viktigst for at andre kan bruke det

- [ ] **Finn konfigurasjonsbitene på bussen.** CS 50 kjenner sin egen
      utrustning — den settes med tre mikrobrytere på kortet: veksler
      rotor/plate, varme el/vann, avfrosting forvarme/bypass. **Tre bit.**
      Finner vi dem, kan integrasjonen konfigurere seg selv i stedet for at hver
      bruker må vite hva de har.
      Kandidater blant de konstante statusbytene: `[3]`, `[7]`, `[12]`,
      `[16]`–`[19]`, `[21]`. NB: `[4]` er nå forklart som alarmbitfelt.
      **NB:** menyen som viser dette (`Test → Informasjon → System`) hører til
      **CI 500**. Et CI 50-panel har bare lysdioder og knapper — ingen fasit å
      lese av. Metoden må derfor være å sammenligne med et annet SL4R/CS 50 med
      annen utrustning, eller å utlede det fra hvilke felt som er konstante.
- [x] ~~Følere for tilvalg som ikke er montert forvirrer brukeren.~~ **LØST:**
      `-55` oversettes til `NAN` → `unavailable` i HA. Entiteten skjuler seg
      selv, og dukker opp hvis føleren monteres senere.
- [ ] **Forvarme for plateveksler-aggregater.** Fjernet hos oss fordi SL4 R har
      rotor, men trengs for at integrasjonen skal dekke plateveksler-varianter.
      Krever noen med slikt aggregat til å avlytte feltet.
- [x] ~~Rigg for å fange det uventede.~~ **GJORT:** anomalifangst med
      læringsperiode, ringbuffer på 40 hendelser, `Anomalier`-teller og
      «Dump anomalier»-knapp. I tillegg `Rå rammelogging` som kan slås av og på
      i drift uten reflash. Dokumentert i README under «Feilsøking og
      innsamling».
- [x] ~~Be om bidrag fra andre.~~ **GJORT:** eget avsnitt i README om hvilke
      logger som er mest verdifulle, og at uenighet er velkomment.

## Åpne, men mindre

- [x] ~~Fjern forvarme-entiteten.~~ **GJORT.** CS 50-manualen er entydig:
      forvarme er en plateveksler-funksjon, og SL4 R har roterende veksler.
      Hele switch-plattformen utgikk med den.
- [x] ~~Døp om «Forvarme aktiv» til «Ettervarme aktiv».~~ **GJORT.** Hele
      komponenten er omdøpt (`afterheat_active`), gammel entitet ryddet bort av
      ESPHome selv. Navnet forvarme var arvet fra Vongraven og er feil for et
      rotoraggregat.
- [x] ~~Bekreft ettervarme-hypotesen.~~ **AVKLART 2026-08-15** for av/på-flagget:
      det ligger i **panelets** ramme, `data[4]` bit7 — verifisert begge veier
      mot panelets «+»-lampe. Styres fra `switch` «Ettervarme», som til
      forskjell fra panelbevegelsen ikke utløser filterreset.
      **MEN:** `payload[6]` bit0 = «elementet varmer» røk i fase 0-analysen
      samme dag (bit0 fulgte forseringen, med ettervarmen deaktivert) —
      se re-verifiseringspunktet under punkt 1.
- [ ] **Egen «avbryt forsering»-knapp.** Å sette gjeldende viftetrinn avbryter
      forseringen (verifisert: pådrag 100 → 49 %). En knapp som bare skriver
      dagens trinn ville gjort det åpenbart.
- [ ] **Viftekommando under aktiv forsering.** Kommandobyten koder (fra, til),
      og «fra» tas fra høy nibbel av statusbyten — som under forsering er 3, ikke
      brukerens valgte trinn. Utestet grensetilfelle.
- [x] ~~**Kartlegg `0xC7`-parameterne.**~~ **DEKODET (fase 0, 2026-08-15):**
      reg `0x0E` = vinterkompensering (Start vinter −20, Stopp vinter −30,
      Temp dif 2, følerkorreksjoner 0×4), reg `0x07` slots 3–6 =
      sommerkompensering (Sommer dif 2, Vinter dif 1, Stopp sommer 30,
      Start sommer 25 — 25 var altså IKKE maks settpunkt). `0.01`×4 og `0.3`×6
      er regulatorforsterkninger (fabrikknivå, ingen tall i manualen).
      Gjenstår: `0, 0.1, 0.1` i reg `0x15`.
- [ ] **Ukeprogrammet (bank `0x21`) — PARKERT.** Strukturen er kjent (dagur 1–4
      + ukeur 1–6, defaults synlige: 06:00, 20 °C, 23:59), men postlayouten kan
      ikke festes uten å endre en ur-innstilling — og det krever CS 500-panel
      med display. Vårt ur er permanent inaktivt på fabrikkdefault. Trenger
      logg fra et CS 500-anlegg, eller skriving mot bank `0x21`.
- [x] ~~**Kryssjekk registrene mot CS50-kortets klemmeliste.**~~ **GJORT** —
      klemmelista er hentet fra Flexits egen CS 50-manual (94269N-02) og
      dokumentert i protokollnotatene. Den forklarte `-55`-slottene (B6,
      platevekslerens frostføler, ikke montert på et rotoraggregat) og bekreftet
      at kun tilluft måles.
- [ ] **Ekstern forseringsbryter på CS 50** (J5 pin 16, «Forsert ventilasjon»)
      ville gitt en uavhengig kontroll på at vår forseringskommando gjør det
      samme som maskinvaren. Kun aktuelt hvis du uansett skal inn i aggregatet.
- [ ] **Last-test bussens strømkapasitet** (100 Ω over 12 V ≈ 120 mA).
      Lav prioritet — ingen brownout gjennom mange OTA-runder, og `Resetårsak`
      + `Oppetid` fanger opp en eventuell svikt.

## Robusthet og drift

- [x] ~~«Kommunikasjon OK» ble stående `off`.~~ **LØST.** Ikke protokolltiming,
      men en `return` i `loop()` som hoppet over helsesjekken. Helsesignalet
      hviler nå på hvilken som helst validert ramme.
- [x] ~~Stille feil hvis vi droppes fra pollerunden.~~ **LØST:**
      `Enumerert på bussen` viser om CS50 fortsatt poller oss.
- [ ] **Hva skjer ved strømbrudd på huset?** Noden og aggregatet henger på samme
      kurs, så begge starter samtidig og vi rakk enumereringen i testen. Men det
      er verifisert én gang. Verdt å bekrefte at det holder konsistent — og
      vurdere et varsel hvis `Enumerert` går av.
- [ ] **Automatiser deploy** til `/config/esphome/`, eller versjoner
      `components/` i addonens nested git, så deploy-målet ikke drifter fra
      kilden. I dag er det manuell `tar | ssh`.

## Til slutt

- [ ] **Flipp repoet til public.** Protokollen er nå dokumentert langt utover
      det som fantes offentlig fra før — poll/svar-modellen, adressefeltet,
      nibbel-kodingene og enumereringen er ikke beskrevet noe annet sted.
      Andre med SL4R/CS50 vil ha nytte av det.

---

## Besvart underveis (kortversjon)

| Spørsmål | Svar |
|---|---|
| Hvorfor ble all sending ignorert? | Bussen er polled; uoppfordret trafikk lyttes ikke på |
| Hvordan melder vi oss på? | Svar på pollen til vår node — enumereres ved CS50s oppstart |
| Blir vår skriving overskrevet av panelet? | Nei, panelet sender kun ved endring |
| Hva er de «sjeldne nodene» 2, 3 og 5? | Parser-artefakter, ikke ekte noder |
| Trengs dipswitch 3 på et fysisk panel? | Nei — vi enumereres som node 5 uten det |
| Hvilken temperaturføler er på bussen? | Kun tilluft (Flexits B1, ettervarmeføler) |
| Hvor mange vifter? | To, ikke fire |
| Hvordan slås forsering av? | Sett viftetrinn |
| Hva er `payload[4]`? | Alarmbitfelt — bit1 = filteralarm |
| Hva er `payload[6]`? | bit0 flipper ved forsering (elementet-varmer-tolkningen røk i fase 0 — re-verifiseres). Bit7 er IKKE enable-flagget |
| Hvor ligger ettervarme av/på? | Panelets `data[4]` bit7 — ikke i statustelegrammet |
| Hvordan slås ettervarme av/på? | `switch` i HA (verifisert), eller hold − og trykk + på panelet |
| Hvorfor gir panelbevegelsen filterreset? | Samme knappekombinasjon; varigheten skiller. Vår bryter unngår det |
| Har vi forvarme? | Nei — plateveksler-funksjon, vi har rotor |
| Er filteralarmen trykkbasert? | Nei — timer («filtertid»), trykkvakt er ikke CS 50 |
| Holder det å trykke begge temp-knappene for filterreset? | Nei — alarmen kom tilbake. Temperaturen må først til 20 grader |
| Finnes rotorpådrag i det hele tatt? | Ja — J5 pin 11,12 (0–10 V), ikke merket «ikke CS 50» |
| Én eller to temperaturfølere? | **Én.** Ettervarmesettet monterer to *termostater* (OT + BT), ikke måleførere |
