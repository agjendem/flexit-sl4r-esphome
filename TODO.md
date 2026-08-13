# TODO — kartlegging av protokollen


Målet er å teste alle varianter som lar seg teste, og dokumentere så mye av
CS50/CI50-protokollen som mulig. Utledningen ligger i
[`research/protocol-notes.md`](research/protocol-notes.md); råopptakene i
[`research/captures/`](research/captures/).

**Arbeidsmåte som har fungert:** slå på `uart: debug:` i
`flexit-atom-lite.yaml`, OTA, fang loggen til fil, parse med
sjekksum-validatoren i `research/captures/README.md`, og *diff mot en kjent
tilstand*. Alle funn så langt kom fra å endre én ting av gangen på panelet og
se hvilke byte som fulgte etter. Skru av debug igjen etterpå — den spiser CPU
fra `loop()`.

---


## Hva vi kan forvente å finne på bussen

Sammenstilt fra Flexits dokumentasjon for CS50/CI50, aggregattypen (SL4 R =
roterende gjenvinner), og det vi faktisk har målt.

### Bekreftet funnet

| Størrelse | Hvor | Notat |
|---|---|---|
| Viftetrinn (kjørende + retur) | status `payload[5]`, to nibbler | `0x31` = forsering |
| Viftepådrag i prosent | status `payload[13]` og `[14]` | 49 / 74 / 100 % |
| Settpunkt varmeveksler | status `payload[9]` (byte) **og** `0xC2` reg 7 (float) | 15–25 °C, to uavhengige representasjoner |
| **Tilluft-temperatur** | `0xC2` reg 0 slot 1 (float) | = Flexits **B1, «tilluftføler ettervarme»** |
| To ledige følerinnganger | `0xC2` reg 0 slot 0 og 4 = `-55` | ikke tilkoblet |
| Forseringskommando | egen ramme `20 14 31 23` | implementert som knapp |
| Regulatorparametere/grenser | `0xC7` reg 0/7/14/21 | konstante over et døgn |

### Dokumentert for CS50 — bør kunne finnes

- **Rotorpådrag.** Flexit oppgir utgang **EB1 (rotor, 0–10 V)**, og at CS50
  regulerer rotoren for å treffe ønsket temperatur i spennet 15–25 °C. Vårt
  «settpunkt varmeveksler» ER altså rotorens reguleringssettpunkt. Da må det
  finnes en pådragsverdi (0–100 % eller 0–10 V) et sted — **ikke funnet ennå**.
  Beste jaktmetode: endre settpunktet på panelet i et kaldt/varmt øyeblikk der
  rotoren faktisk må jobbe, og se hvilken verdi som beveger seg med.
- **Rotorvakt.** CS50 har rotorvakt-funksjon → forvent et alarm-/statusbit.
- **Ettervarme.** B1 er ettervarmens føler, så ettervarmens pådrag eller
  av/på-tilstand bør ligge på bussen.
- **Forvarme.** Koden vår antar `payload[6]` = 0/128, men feltet veksler 0/1 i
  praksis. Må avklares ved å slå forvarme av og på.
- **Filtervakt-timer og alarmflagg**, samt **overhetingstermostat-alarm**.

### Antakelser som må korrigeres

- **Antall vifter: to, ikke fire.** SL4 R er et aggregat med tilluftsvifte og
  avtrekksvifte, og målingene støtter det — det er nøyaktig to
  prosentverdier (`payload[13]` og `[14]`) som følger viftetrinnet.
  Finnes det fire pådrag et sted, er de ikke sett ennå.
- **Kun tilluft måles av CS50.** Avtrekk, avkast og uteluft finnes ikke på
  bussen i vår konfigurasjon — brukerens fire Z-Wave-følere er altså ikke
  overflødige, og virkningsgradregnestykket må fortsatt hvile på dem.


## 1. Sending — verifisere det vi allerede har bygget

- [ ] **Test forseringsknappen ende-til-ende.** Vent til pågående forsering har
      løpt ut, slå på rå-logging, trykk `button.…_forsering` i HA, og bekreft
      både at rammen går ut korrekt og at CS50 svarer med `0x31` i statusen.
      Dette blir vår aller første sending på bussen.
- [ ] **Fang «forsering av».** Vi har aldri sett en avslutning — opptaket
      sluttet mens forseringen løp. Skjer det av seg selv (timeout) eller
      sender panelet noe? Lytt gjennom en hel forseringsperiode.
- [ ] **Avklar `data[2]` i kommandorammen**: `02` hos oss mot `00` hos
      Vongraven. Modellforskjell, konfigurasjon, eller noe som varierer?
- [x] ~~**Blir vår skriving overskrevet?**~~ **NEI — avklart 2026-08-14.**
      Panelets tilstandsramme sendes KUN ved endring: 21 av 21 observerte lå
      inntil en tilstandsendring, med opphold på opptil 533 rammer mellom.
      CS50 holder tilstanden selv. Forseringen bekrefter det uavhengig — status
      sto på `0x31` i timevis mens panelets siste melding sa `0x11`.
- [x] ~~Avklar `data[2]`~~ — vi bruker nå VÅR egen målte ramme (`0x02`) i stedet
      for Vongravens gjetning (`0x00`). Betydningen er fortsatt ukjent, men
      irrelevant så lenge vi sender det anlegget selv sender.
- [ ] **Skru på skriving av viftetrinn og settpunkt** (indeks 11 og 15) —
      men FØRST etter at forseringsknappen er testet, se punktet over.
- [ ] **Avklar indeks 12 før forvarme skrives.** Feltet gikk fra `00` til `01`
      i rammen fem rammer før forseringskommandoen, altså ved «Max vifte» —
      ikke ved noe forvarmerelatert. Slå forvarmen av og på mens bussen logges.
      Til da: hold forvarme utenfor skriverunden.

## 2. Alarmer — fang ved neste filterbytte

- [ ] **Filterreset.** Prosedyre fra CI 50-manualen: still temperaturen til 20
      grader, trykk begge temperaturknappene (◄ og ►) samtidig. Ta opp
      **før og etter** — det gir to ting på én gang: reset-kommandoen, og
      hvilken byte som bærer alarmflagget.
      **Ikke nullstill uten at filteret faktisk byttes** — timeren starter på
      nytt og det ekte vedlikeholdsvarselet går tapt.
- [ ] **Alarmflagget** er sannsynligvis mer verdt enn resetknappen: et
      filtervarsel i HA er reell nytte. Kandidater i statusens payload:
      `[2]` (sett med 0/36/72/144) og `[4]` (konstant `02` hos oss, `00` hos
      Vongraven).
- [ ] Overhetingstermostat: kan ikke fremprovoseres trygt. Hvis den noen gang
      løser ut — ta opp før reset.

## 3. Flyttall-registrene — størst uutnyttet potensial

Se «Flyttall-registre» i protokollnotatene. `0xC2`/`0xC7` bærer IEEE754 float.

- [x] ~~**Identifiser temperaturføleren** i `0xC2` reg 0 slot 1.~~
      **TILLUFT** — Flexits B1, «tilluftføler ettervarme». Identifisert
      2026-08-14 ved korrelasjon mot brukerens fire Z-Wave-følere i kanalen.
- [x] ~~**Kartlegg de andre slottene i `0xC2` reg 0.**~~ To står på `-55` =
      føler ikke tilkoblet; resten er `0`. Bussen har altså KUN én tilkoblet
      temperaturføler.
- [ ] **Kryssjekk registrene mot CS50-kortets fysiske klemmer.** CS50 har
      mange tilkoblingsmuligheter — ekstra temperaturfølere, ekstern
      forseringsbryter, og andre tilvalg — og de fleste er ikke i bruk hos
      oss. Det er nesten sikkert derfor flere float-slots står på `-55` og
      flere statusbyte er konstant null. Skaff koblingsskjemaet for CS50
      (aggregatets egen manual, ikke CI 50-manualen) og legg klemmelista side
      om side med registerkartet: da får hvert ubrukt slot et navn, og vi
      slipper å gjette på felt som aldri kommer til å variere.
      Det åpner samtidig en dør: en **ekstern forseringsbryter** på CS50 ville
      gitt en fysisk måte å utløse forsering på, som kan sammenlignes med
      rammen vi sender — en uavhengig kontroll på at vi gjør det riktig.
- [x] ~~**Eksponer temperaturene som `sensor` i HA.**~~ Gjort 2026-08-14:
      Tilluft, viftepådrag ×2, viftetrinn kjørende/retur, settpunkt fra buss,
      «Forsering aktiv», og begge ledige følerinnganger. I tillegg
      `raw_status_bytes` og `float_registers` for å eksponere vilkårlige felt
      fra YAML uten C++-endring.
- [ ] Kartlegg `0xC7`-parameterne (`0.01`, `0.3`, `2`, `1`, `30`, `25`,
      `-20`, `-30`). Sannsynligvis regulatorparametere og grenseverdier — kan
      de leses mot noe i aggregatets servicemeny?

## 4. Ukjente felt i statustelegrammet

Payload-indekser som varierte i opptaket, men uten kjent betydning:

- [ ] `[2]`: `0 / 36 / 72 / 144` — dobling, ser ut som bitfelt eller teller
- [ ] `[6]`: veksler `0/1` omtrent 50/50 — **NB:** koden vår antar at forvarme
      er `0/128` her. Enten er feltet noe annet, eller så er kodingen `0/1`.
      Må avklares ved å slå forvarme av og på.
- [ ] `[11]`: `0/1` — koden vår behandler den som forvarme-terskel («inaktiv
      når < 100»), noe som ikke stemmer med observerte verdier
- [ ] `[15]`: `32/35`, `[20]`: `68/136` — veksler i takt med `[6]`?
      Kandidat: sekvensteller eller blinkefase for panelets lysdioder
- [ ] `[12]`, `[16]`–`[19]`, `[21]`: ikke observert variasjon ennå

Bekreftet: `[5]` viftetrinn (to nibbler), `[9]` settpunkt,
`[13]`/`[14]` viftepådrag i prosent.

## 5. Adressering og topologi

- [ ] **Dipswitch 3-testen.** Fem avsendersignaturer er observert:
      `01 00 C4 4B` (CS50), `04 00 C7 51` (CI50), og de sjeldne
      `02 00 C5 4D`, `03 00 C6 4F`, `05 00 C8 53`. Byte 1 ser ut som node-ID.
      Flipp dip 3 (OFF=PANEL 1, ON=PANEL 2 — fabrikk er OFF) og se om
      panelets signatur endrer seg. **Sett den tilbake etterpå.**
      Merk: dipswitchen leses trolig kun ved oppstart, og panelet må uansett
      åpnes fysisk — se protokollnotatene punkt 5.
- [ ] Hva er de tre sjeldne signaturene (10 rammer hver)? Andre noder,
      broadcast, eller en oppstarts-/skannesekvens?
- [ ] Hva betyr `b6` (offset 6 i rammen)? Konstant `04` i panelrammene.

## 6. Fysisk

- [ ] **Last-test bussens strømkapasitet** med en motstand (100 Ω over 12 V ≈
      120 mA). Kildeimpedansen er aldri målt. Ingen brownout er observert
      gjennom mange OTA-runder, så dette er lav prioritet — `Resetårsak` og
      `Oppetid` fanger opp en eventuell svikt.

## 7. Opprydding

- [ ] Eksponer «Forsering aktiv» som `binary_sensor` — lav nibbel ≠ høy nibbel
      i `payload[5]` er en presis indikator, og den finnes allerede i dataene.
- [ ] Vurder å versjonere `components/` i `/config/esphome/` sin nested git,
      eller å automatisere kopieringen fra dette repoet, så deploy-målet ikke
      drifter fra kilden.
- [ ] Flipp repoet til public når protokollen er godt nok dokumentert til å
      være til nytte for andre med SL4R.
