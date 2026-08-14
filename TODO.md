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

- [ ] `[2]`: `0 / 36 / 72 / 144` — dobling, ser ut som bitfelt eller teller
- [ ] `[11]`: `0/1`
- [ ] `[15]`: `32/35`, `[20]`: `68/136` — veksler disse i takt med `[6]`?
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
- [ ] Fang **reset-kommandoen** når filteret faktisk byttes: still temperaturen
      til 20 grader og trykk begge temperaturknappene samtidig (CI 50-manualen).
      **Ikke nullstill uten at filteret byttes** — timeren starter på nytt.
- [ ] Sjekk om **filtertiden/tidstelleren** også ligger på bussen. Da kunne HA
      vist «dager til filterbytte» i stedet for bare en alarm.

### 3. Rotorpådrag og ettervarme — fyringssesongen

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
- [x] ~~Bekreft ettervarme-hypotesen.~~ **BEKREFTET 2026-08-14** ved å slå
      ettervarmen av og på fra panelet. `payload[6]` er et bitfelt:
      bit0 = elementet varmer nå, bit7 = DEAKTIVERT (invertert!). To entiteter
      nå, som svarer til panelets to lysdioder. Vongravens terskellogikk var
      unødvendig og er fjernet.
- [ ] **Egen «avbryt forsering»-knapp.** Å sette gjeldende viftetrinn avbryter
      forseringen (verifisert: pådrag 100 → 49 %). En knapp som bare skriver
      dagens trinn ville gjort det åpenbart.
- [ ] **Viftekommando under aktiv forsering.** Kommandobyten koder (fra, til),
      og «fra» tas fra høy nibbel av statusbyten — som under forsering er 3, ikke
      brukerens valgte trinn. Utestet grensetilfelle.
- [ ] **Kartlegg `0xC7`-parameterne** (`0.01`, `0.3`, `2`, `1`, `30`, `25`,
      `-20`, `-30`). Konstante over døgn, så trolig regulatorparametere og
      grenseverdier. Kan de gjenfinnes i aggregatets servicemeny?
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
| Hva er `payload[6]`? | Bitfelt — bit0 = varmer nå, bit7 = deaktivert (invertert) |
| Hvordan slås ettervarme av/på? | Hold − og trykk + på panelet |
| Har vi forvarme? | Nei — plateveksler-funksjon, vi har rotor |
| Er filteralarmen trykkbasert? | Nei — timer («filtertid»), trykkvakt er ikke CS 50 |
| Holder det å trykke begge temp-knappene for filterreset? | Nei — alarmen kom tilbake. Temperaturen må først til 20 grader |
| Finnes rotorpådrag i det hele tatt? | Ja — J5 pin 11,12 (0–10 V), ikke merket «ikke CS 50» |
| Én eller to temperaturfølere? | **Én.** Ettervarmesettet monterer to *termostater* (OT + BT), ikke måleførere |
