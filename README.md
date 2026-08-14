# Flexit SL4R (CS50/CI50) — ESPHome-integrasjon via RS485

Toveis integrasjon av et Flexit SL4R-ventilasjonsaggregat (CS50-hovedkort,
CI50-betjeningspanel) i Home Assistant, uten Flexit sin egen CI66-adapter.
Leser status (viftetrinn, forvarme, settpunkt varmeveksler) og — når Fase 2
er verifisert med maskinvare — sender kommandoer tilbake.

Maskinvare: M5Stack **ATOM Lite** (ESP32) + **ATOM Tail485** (TTL↔RS485,
SKU T002), koblet i parallell med CI50 i den ledige **4P4C**-kontakten bak på
betjeningspanelet, og matet fra bussens egne 12 V (målt 11,8 V). Pinout og
strømforsyning: se
[`research/protocol-notes.md`](research/protocol-notes.md) → «Fysisk tilkobling».

## Status

**Toveis styring virker mot ekte anlegg (14. august 2026).**

- **Lesing:** tilluftstemperatur, viftetrinn (kjørende + retur), viftepådrag i
  prosent for begge vifter, settpunkt varmeveksler, forsering aktiv, samt en
  rekke diagnostikk-entiteter for felt vi ennå ikke har tydet.
- **Skriving:** viftetrinn, settpunkt varmeveksler og forsering — alt verifisert
  mot CS50s egne kringkastede verdier, ikke bare mot vår egen UI-tilstand.
  Å sette viftetrinn avbryter samtidig en pågående forsering.
- **Ikke aktivert:** forvarme. Flagg-byten i tilstandsrammen er uavklart, og vi
  gjetter ikke på et felt som kan utløse noe annet enn det står på.
- **Driftsdiagnostikk:** `Enumerert på bussen` viser om CS50 fortsatt poller
  oss. Går den av, feiler skriving *stille* — enumereringen skjer kun ved
  aggregatets oppstart, så da må aggregatet strømsykles. `Kommunikasjon OK`
  dekker mottakssiden, og `Statusintervall` viser hvor ofte statustelegrammet
  kommer (~0,7–1,2 s).

Noden er koblet på CI50-panelets ledige 4P4C-kontakt og matet fra bussens egne
11,8 V.

### Hvordan det henger sammen

Bussen er **polled**. Masteren sender en 5-byte poll til én node av gangen, og
kun den adresserte noden svarer:

```
POLL  (fra master):  C3 <node> 00 <ck1> <ck2>
SVAR  (fra noden):   <TYPE> <node> <LEN> <data...> <ck1> <ck2>
```

Ved oppstart pollast node 2, 3 og 5 fem ganger hver. Svarer de ikke, droppes de
resten av driftsperioden. Vi melder oss derfor på som **node 5 = panel 2** —
identiteten dipswitch 3 konfigurerer på et fysisk panel — og blir enumerert og
pollet videre, også gjennom våre egne omstarter.

Det tok lang tid å komme dit, fordi den opprinnelige modellen tolket poll og
svar som ÉN ramme med et 8-byte hode. Se
[`research/protocol-notes.md`](research/protocol-notes.md) for hele utledningen,
inkludert blindveiene — de er dokumentert med vilje, siden flere av dem så
overbevisende riktige ut.

Protokollarbeidet startet fra
[Vongraven/Flexit-SL4R-master](https://github.com/Vongraven/Flexit-SL4R-master)
(MIT, 2018). Sjekksumalgoritmen derfra stemmer eksakt; rammemodellen måtte
bygges om fra bunnen.

## Ulike aggregatvarianter

SL4R/CS 50 finnes i mange utstyrskombinasjoner — roterende eller plateveksler,
elektrisk eller vannbatteri, med eller uten ettervarme, bypass eller forvarme.
Denne integrasjonen håndterer det på tre nivåer:

**1. Følere detekteres av seg selv.** CS 50 rapporterer `-55` for en
følerinngang som ikke er tilkoblet. Komponenten oversetter det til `NAN`, som
blir `unavailable` i Home Assistant. En entitet for et tilvalg du ikke har
skjuler seg altså selv, uten konfigurasjon — og dukker opp den dagen føleren
monteres.

**2. Alle entiteter er valgfrie.** Hver eneste entitet er `cv.Optional` i
plattformskjemaene, så `flexit-atom-lite.yaml` er et *eksempel*, ikke et krav.
Ta med det du har bruk for.

**3. Funksjoner som er fysisk umulige, utelates.** Forvarme er for eksempel en
plateveksler-funksjon (mikrobryter 3 på kortet: «bare ved plateveksler»), så på
et rotoraggregat som SL4 R kan en forvarme-bryter aldri virke. Den er derfor
ikke med. Har du plateveksler, er dette et punkt som må implementeres — se
[`TODO.md`](TODO.md).

**Automatisk gjenkjenning er innen rekkevidde.** CS 50 vet selv hva den er
utstyrt med; panelmenyen `Test → Informasjon → System` viser
`Gjenvinner: Rotor/plate`, `Varme: Elbat/vannbat` og `Avfrosting: Forvarme/Bypass`
— altså de tre mikrobryterne på kortet. Ligger de bitene på bussen, kan
integrasjonen konfigurere seg selv. Å finne dem er et åpent punkt.

## Repo-struktur

```
components/flexit_sl4r/   ESPHome external_component (C++ hub + sensor/select/switch/number/binary_sensor/button)
research/                 Kildemateriale + protokollutledning (se research/README.md)
research/captures/        Rå bussopptak fra eget anlegg, med parse-oppskrift
TODO.md                   Kartleggingsbacklog — hva som skal testes og dokumenteres
flexit-atom-lite.yaml     Eksempel-/produksjonskonfig for ATOM Lite + Tail485
secrets.yaml.example      Mal for secrets.yaml (wifi/api/ota)
```

Målet framover er å teste alle varianter som lar seg teste og dokumentere så
mye av protokollen som mulig — se [`TODO.md`](TODO.md).

## Kom i gang

```bash
python3.12 -m venv .venv-esphome
./.venv-esphome/bin/pip install esphome
cp secrets.yaml.example secrets.yaml   # fyll inn wifi + generer api-nøkkel/ota-passord
./.venv-esphome/bin/esphome run flexit-atom-lite.yaml
```

(`python3.12` fordi ESPHome pt. ikke støtter Python 3.14, som er standard
`python3` på denne utviklingsmaskinen.)

## Videre plan

Kartleggingen fortsetter i [`TODO.md`](TODO.md). De største åpne punktene:

1. **Rotorpådraget.** Flexit oppgir utgang EB1 (rotor, 0–10 V), og settpunktet
   vi nå styrer ER rotorens reguleringsmål — men pådragsverdien er ikke funnet.
   Best jaktet når rotoren faktisk må jobbe, altså i fyringssesongen.
2. **Filtervakt-alarmen.** Fanges ved neste filterbytte: opptak før og etter
   reset gir både alarmbyten og reset-kommandoen. Alarmflagget er trolig mer
   verdt enn resetknappen — et filtervarsel i HA er reell nytte.
3. **Forvarme-skriving**, når flagg-byten er avklart.
4. **De ukjente statusfeltene** ligger eksponert som diagnostikk, så HAs
   recorder bygger historikk å korrelere mot uten nye bussopptak.

## Hvor koden kjører

Firmwaren bygges **ikke** fra dette repoet i praksis. 3. august 2026 ble
`components/flexit_sl4r/` og YAML-en kopiert til HA-vertens ESPHome-addon som
`/config/esphome/flexit-sl4r.yaml` + `/config/esphome/components/flexit_sl4r/`,
og flashet derfra. Det er den kopien som faktisk kjører på noden.

Dette repoet er dermed **kilden/utviklingsstedet**, og `/config/esphome/` er
**deploy-målet**. De kan drifte fra hverandre — per 13. august 2026 er
HA-kopien én revisjon bak, men forskjellen er kun kommentarer. Ved endringer i
komponenten: rediger her, kopier over, bygg der. Sjekk drift med:

```bash
ssh anders@192.168.1.205 'cat /config/esphome/flexit-sl4r.yaml' | diff -u flexit-atom-lite.yaml -
```

(`/config/esphome/` er en egen nested git fra ESPHome-addonen og er ekskludert
fra HA-config-repoet — se `CLAUDE.md` i homeassistant-workspace.)

## Kilder og kreditering

Protokollkunnskapen her hviler på **[Vongraven/Flexit-SL4R-master](https://github.com/Vongraven/Flexit-SL4R-master)**
(MIT, 2018) — en Arduino Mega-implementasjon testet mot ekte SL4R/CS50, og
eneste kjente kilde til både protokollen og koblingsskjemaet for denne bussen.
Dette repoet er ikke en fork: det deler ingen kodelinjer, og protokollen er
reimplementert og verifisert numerisk på nytt. Se
[`research/README.md`](research/README.md) for nøyaktig hva som er kopiert
derfra og hva som er vår egen utledning.

Øvrige kilder brukt i utledningen:

- [patstave/Node-FlexitCS60-RS485](https://github.com/patstave/Node-FlexitCS60-RS485)
  — dokumentert RJ12-pinout for søstergenerasjonen CS60 (+12 V på pinne 5–6),
  som underbygger tolkningen av CS50s 4P4C-kontakt.
- [M5Stack Tail RS485, SKU T002](https://docs.m5stack.com/en/atom/tail485) —
  [datablad med blokkdiagram](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/atom/tail485.pdf)
  (9–24 V buck, SP485EEN-L, G26/G32, ingen DE/RE-linje).
- Flexits egne manualer: [CI 50 styrepanel](https://www.flexit.no/globalassets/catalog/documents/man_110191n_3748.pdf)
  (2 kontakter bak på panelet, dipswitch 3 = PANEL 1/2) og
  [Modbusadapter CI 66](https://www.flexit.no/produkter/relatert/modbusadapter_ci66_k2-c2-uni/)
  (RJ12 1–1 og D0/D1/Common/VP-navngivning på nyere generasjon).
- [hjemmeautomasjon.no: «Styre balansert ventilasjon, Flexit CI60»](https://www.hjemmeautomasjon.no/forums/topic/714-styre-balansert-ventilasjon-flexit-ci60/)
  — Vongravens egen beskrivelse av tilkoblingen, og erfaringer fra CS60/CI60.

## Lisens

MIT, se [`LICENSE`](LICENSE). Kopiert kildemateriale i `research/` er
Copyright (c) 2018 Vongraven, også MIT — se
[`research/LICENSE-Vongraven`](research/LICENSE-Vongraven).
