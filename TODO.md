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
- [ ] `[6]`: veksler `0/1` ~50/50 — kandidat: forvarme aktiv, eller en blinkefase
- [ ] `[11]`: `0/1`
- [ ] `[15]`: `32/35`, `[20]`: `68/136` — veksler disse i takt med `[6]`?
- [ ] `[12]`, `[16]`–`[19]`, `[21]`: ingen variasjon observert ennå

Korrelér mot noe som endrer seg — utetemperatur, viftetrinn, tid på døgnet.
Bekreftet fra før: `[5]` viftetrinn, `[9]` settpunkt, `[13]`/`[14]` viftepådrag.

### 2. Filtervakt-alarmen — ved neste filterbytte

Klart størst praktisk nytte som gjenstår: et **filtervarsel i HA**.

- [ ] Ta opp bussen **før og etter** en filterreset. Det gir to ting på én gang:
      hvilken byte som bærer alarmflagget, og selve reset-kommandoen.
- [ ] Prosedyre (CI 50-manualen): still temperaturen til 20 grader, trykk begge
      temperaturknappene samtidig.
      **Ikke nullstill uten at filteret faktisk byttes** — timeren starter på
      nytt og det ekte vedlikeholdsvarselet går tapt.
- [ ] Kandidatbyte for alarmflagget: `[2]` og `[4]`.

### 3. Rotorpådraget — fyringssesongen

Flexit oppgir utgang **EB1 (rotor, 0–10 V)**, og settpunktet vi nå styrer ER
rotorens reguleringsmål. Da må pådraget ligge et sted, men det er ikke funnet.

- [ ] Endre settpunktet når rotoren faktisk må jobbe, og se hvilken verdi som
      følger med. Nå i august er differansen ute/inne for liten til å skille et
      pådrag fra en konstant — dette er et **høstforsøk**.
- [ ] Sjekk samtidig etter **rotorvakt**-status; CS50 har den funksjonen.

## Åpne, men mindre

- [ ] **Forvarme-skriving.** Flagg-byten (`data[4]`) er uavklart: panelet satte
      den `00`→`01` rett før en FORSERINGS-kommando, ikke ved noe
      forvarmerelatert. `set_preheat()` avviser derfor med en advarsel.
      **Vurder samtidig om entiteten skal fjernes helt** — CI 50-panelet har
      ingen forvarmeknapp, så feltet er kanskje ikke brukerstyrt i det hele tatt.
- [ ] **Egen «avbryt forsering»-knapp.** Å sette gjeldende viftetrinn avbryter
      forseringen (verifisert: pådrag 100 → 49 %). En knapp som bare skriver
      dagens trinn ville gjort det åpenbart.
- [ ] **Viftekommando under aktiv forsering.** Kommandobyten koder (fra, til),
      og «fra» tas fra høy nibbel av statusbyten — som under forsering er 3, ikke
      brukerens valgte trinn. Utestet grensetilfelle.
- [ ] **Kartlegg `0xC7`-parameterne** (`0.01`, `0.3`, `2`, `1`, `30`, `25`,
      `-20`, `-30`). Konstante over døgn, så trolig regulatorparametere og
      grenseverdier. Kan de gjenfinnes i aggregatets servicemeny?
- [ ] **Kryssjekk registrene mot CS50-kortets klemmeliste.** Flere float-slots
      står på `-55` (føler ikke tilkoblet) og flere statusbyte er konstant null,
      nesten sikkert fordi tilvalg ikke er montert. Aggregatets egen manual
      (ikke CI 50-manualen) ville gitt hvert ubrukt slot et navn.
      En **ekstern forseringsbryter** på CS50 ville dessuten gitt en uavhengig
      kontroll på at vår forseringskommando gjør det samme som maskinvaren.
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
