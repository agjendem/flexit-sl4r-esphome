# Flexit SL4R (CS50/CI50) — ESPHome-integrasjon via RS485

> **Dette er den norske versjonen.** [`README.md`](README.md) er engelsk og er
> hovedversjonen for prosjektet. Denne holdes ved like fordi forskningsnotatene
> er skrevet på norsk, men den kan ligge litt etter den engelske.
> Protokollspesifikasjonen finnes på engelsk i [`PROTOCOL.md`](PROTOCOL.md).

Toveis integrasjon av et Flexit SL4R-ventilasjonsaggregat (CS50-hovedkort,
CI50-betjeningspanel) i Home Assistant, uten Flexit sin egen CI66-adapter.
Leser status og skriver kommandoer over aggregatets egen RS485-buss.

Maskinvare: M5Stack **ATOM Lite** (ESP32) + **ATOM Tail485** (TTL↔RS485,
SKU T002), koblet i parallell med CI50 i den ledige **4P4C**-kontakten bak på
betjeningspanelet, og matet fra bussens egne 12 V (målt 11,8 V). Pinout og
strømforsyning: se
[`research/protocol-notes.md`](research/protocol-notes.md) → «Fysisk tilkobling».

## Status

**Toveis styring virker mot ekte anlegg (14. august 2026).** Siden da er
`climate`-entitet, parameterregistrene og filtertelleren kommet til, og det er
bekreftet at parameterregistrene kan skrives (15. august).

- **Lesing:** tilluftstemperatur, viftetrinn (kjørende + retur), viftepådrag i
  prosent for begge vifter, settpunkt, varmepådrag, forsering, **filteralarm**,
  **ettervarme aktivert**, og fra parameterregistrene **timer siden
  filternullstilling** med filterintervall. I tillegg diagnostikk-entiteter for
  felt vi ennå ikke har tydet, slik at HAs recorder bygger historikk.
- **Skriving:** viftetrinn, settpunkt, forsering, avbryt forsering, ettervarme
  av/på og filterreset — alt verifisert mot CS50s egne kringkastede verdier,
  ikke bare mot vår egen UI-tilstand.
- **`climate`-entitet** «Ventilasjon» samler settpunkt, viftemodus,
  BOOST-preset og HEAT/FAN_ONLY (ettervarme) i én termostat-modell.
  Viftemodusene bruker HAs **standardmoduser** `low`/`medium`/`high`, slik at
  frontenden oversetter dem selv — på norsk vises de som **Lav / Middels /
  Høy**. De tilsvarer Flexits egne beskrivelser i CI 50-manualen: trinn 1
  «redusert ventilasjon» (low), trinn 2 «normal drift» (medium) og trinn 3
  «økt ventilasjon i våtrom» (high). NB: `high` er en varig innstilling, mens
  BOOST er den tidsbegrensede maksimalviften aggregatet går ut av selv.
  `select`-entiteten «Viftetrinn» beholder 1/2/3, siden den speiler panelets
  tre lysdioder.
- **Driftsdiagnostikk:** `Enumerert på bussen` viser om CS50 fortsatt poller
  oss. Går den av, feiler skriving *stille*, og aggregatet må strømsykles.
  `Kommunikasjon OK` dekker mottakssiden, og `Statusintervall` viser hvor ofte
  statustelegrammet kommer (~0,7–1,2 s).

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
resten av driftsperioden — målt på en kaldstart: node 2 og 3 fikk sine fem
forsøk innen henholdsvis 371 og 394 ms, og forsvant deretter. Vi melder oss derfor på som **node 5 = panel 2** —
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

## Entiteter

**Styring**

| Entitet | Type | Merknad |
|---|---|---|
| Viftetrinn | `select` | 1–3. Å sette trinn avbryter en pågående forsering |
| Settpunkt varmeveksler | `number` | 15–25 °C |
| Forsering | `button` | «Max vifte» — aggregatet faller selv tilbake |
| Avbryt forsering | `button` | Skriver returtrinnet |
| Ettervarme | `switch` | Skriver flagget direkte — utløser ikke filterreset, slik panelbevegelsen gjør |
| Nullstill filtervakt | `button` | Kjører hele manualens prosedyre automatisk |
| Ventilasjon | `climate` | Alt det over i én termostat-modell |

**Måling**

Tilluftstemperatur · **varmepådrag** (0–100, styrer rotorens hastighet) ·
viftepådrag tilluft/avtrekk (%) · viftetrinn kjørende og retur · settpunkt lest
fra bussen · ledige følerinnganger (skjuler seg selv når de ikke er tilkoblet).

**Tilstand og alarm**

Filteralarm · **ukjent alarm** · ettervarme aktivert · forsering aktiv ·
**varmegjenvinner går** (rotoren snurrer faktisk, ikke bare at det finnes et
behov) · **bypass (antatt)**.

«Ettervarme aktivert» svarer til panelets «+»-lampe og kan verifiseres mot den
direkte. «Ukjent alarm» er ethvert bit i alarmfeltet utenom filterbiten — vi vet
ikke hvilket bit som er rotoralarm og hvilket som er overhetingstermostat, men
entiteten fanger dem den dagen de fyrer, og rådataene ligger i anomaliloggen.

«Bypass (antatt)» er en åpen gjetning, og navnet sier det: biten er aldri
observert satt på vårt rotoraggregat. Hypotesen er at den koder bypass på
plateveksleraggregater. Endrer den seg, logges det som anomali med full ramme.

**Diagnostikk**

Kommunikasjon OK · enumerert på bussen · anomalier · statusintervall · rammer
forkastet · resetårsak · oppetid · rå statusbyte for felt som ennå ikke er tydet
· **firmwareversjon for styrekort og panel**.

Firmwareversjonene leses rett fra bussen, og er verdt å oppgi i feilrapporter:
alt i protokollnotatene er utledet fra styrekort `R1A 2.8` og panel `R1A 1.2`.

## Hvordan skriving oppfører seg

**En skriving endrer kun feltet du ba om. Alt annet speiles.**

Panelets tilstandsramme inneholder flere felt i samme melding — viftetrinn,
settpunkt, ettervarme og noen vi ennå ikke forstår. Endrer du settpunktet, må de
øvrige feltene likevel fylles ut, og da er det fristende å skrive konstanter.

Det gjør denne integrasjonen **ikke**. Hver skriving bygger på panelets sist
kjente ramme, og kun det ene feltet du faktisk endrer blir overstyrt.

Grunnen er erfaring, ikke prinsipprytteri. Vi hardkodet først to felt vi trodde
var konstante, og oppdaget senere at det ene var ettervarmens av/på-flagg — så
hver settpunkt- eller viftetrinn-endring slo av ettervarmen i aggregatet. Med
speiling kan den feilen ikke gjenoppstå, heller ikke i felt vi ennå ikke har
tydet.

To følger er verdt å merke seg:

- **Les tilstand fra statustelegrammet, ikke fra panelrammen**, når begge har
  den. Panelet sender kun ved *endring*, så en verdi som bare latches derfra kan
  være timer gammel — og etter en omstart står den på sin default. Ettervarmen
  ble lest slik en periode, og siden verdien speiles inn i alle utgående rammer,
  slo den første viftekommandoen etter hver omstart av ettervarmen. Nå leses den
  fra statustelegrammet, som kommer hvert sekund.
- **Knappebitene settes aldri** med mindre en handling krever det. Derfor kan
  for eksempel `switch` «Ettervarme» ikke utløse filterreset som bieffekt —
  noe panelets egen knappebevegelse faktisk gjør.

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

**Automatisk gjenkjenning er trolig mulig.** CS 50 vet selv hva den er utstyrt
med — det settes med tre mikrobrytere på kortet (veksler rotor/plate, varme
el/vann, avfrosting forvarme/bypass). Ligger de bitene på bussen, kan
integrasjonen konfigurere seg selv.

Merk at menyen som viser dette (`Test → Informasjon → System`) hører til
**CI 500**, ikke CI 50. Et CI 50-panel har bare lysdioder og knapper, så på et
SL4R-anlegg finnes ingen enkel fasit å lese av — bitene må eventuelt finnes ved
å sammenligne anlegg med ulik utrustning. Åpent punkt, se [`TODO.md`](TODO.md).

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

Kartleggingen fortsetter i [`TODO.md`](TODO.md). Rotorpådraget (`[11]`),
filteralarmen (`[4]` bit1) og til og med driftstimetellerne er funnet — de
største åpne punktene nå:

1. **Ur-lagringen (bank `0x21`)** — nå dekodbar, siden parameterregistrene
   viste seg å være skrivbare: skriv ett felt og se hva som flytter seg.
2. **Ettervarmens eget 0–10 V-pådrag** (J5 pin 9,10). Vi har ingen
   «elementet varmer nå»-indikator i det hele tatt — den antatte viste seg
   å være forseringsflagget. Fyringssesongen gir bedre signal.
3. **Rød alarm-LED**: hvilke bit i `[4]` er rotoralarm og
   overhetingstermostat? Fanges automatisk av «Ukjent alarm» + anomaliloggen.
4. **`[15]`** — varierer (32/35/48/51) uavhengig av både forsering og
   ettervarme. Eneste statusfelt uten kjent korrelat.
5. **Bidrag fra andre anlegg** (plateveksler, CS 500) for bypass-biten og
   utstyrskonfigurasjonen — de kan ikke avgjøres på vårt aggregat.

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

## Bidrag og logger ønskes

Dette er reverse-engineering av en udokumentert protokoll, gjort på **ett**
anlegg. Mye av det som står her gjelder sikkert bredere enn vi kan vite, og noe
gjelder sikkert bare oss. Derfor: **logger fra andre SL4R/CS 50-anlegg er svært
velkomne** — særlig fra aggregater med annen utrustning enn vårt (plateveksler,
vannbatteri, forvarme, to paneler).

Konkret er dette mest verdifullt:

- **Et oppstartsopptak.** Trykk «Dump oppstartsfangst» etter at aggregatet har
  vært strømsyklet. Det viser enumereringen og hvilke noder som finnes.
- **Anomalier.** Integrasjonen fanger automatisk det uventede (se under).
  Trykk «Dump anomalier» når noe har skjedd, og lim inn loggen.
- **Hva panelet viser** i samme øyeblikk. Lysdiodene på CI 50 er fasiten vi
  sammenligner bytene mot, og det var nettopp slik ettervarme-bitene og
  filteralarmen ble knekt.
- **Hvilken utrustning aggregatet ditt har** — veksler, varmebatteri,
  ettervarme, avfrosting.
- **Firmwareversjonene dine** (entitetene «Styrekort-firmware» og
  «Panel-firmware»). Avviker de fra våre, er det første sted å lete hvis noe
  ikke stemmer.
- **Har du plateveksler i stedet for rotor?** Da er du særlig interessant. Ett
  bit i statustelegrammet (`[2]` bit 1) har aldri vært satt hos oss, og
  strukturen tyder på at det koder bypass-tilstand — noe bare et
  plateveksleraggregat kan vise.

Åpne en issue med loggen. Uenighet er også nyttig: flere av Vongravens
opprinnelige tolkninger viste seg å være feil for vårt anlegg, og det er fullt
mulig at noen av våre er feil for ditt.

## Feilsøking og innsamling

Integrasjonen er rigget for å samle bevis uten at du må sitte og vente.

**Anomalifangst (alltid på).** Den logger *det uventede*, ikke alt: nye
rammetyper, endringer i felt vi tror er konstante, og enhver endring i
alarmfeltet. De siste 40 hendelsene lagres med full ramme og tidsstempel.
Fordi den bare reagerer på avvik, koster den nesten ingenting å ha stående —
i normal drift teller den null. Sensoren `Anomalier` viser antallet; knappen
**«Dump anomalier»** skriver dem ut.

De første 30 sekundene etter oppstart er en **læringsperiode** der noden lærer
anleggets normale repertoar av rammetyper uten å melde fra. Etterpå er enhver
ny signatur en ekte hendelse.

**Rå rammelogging (av/på i drift).** Bryteren `Rå rammelogging` skriver hver
validerte ramme som hex — uten reflash. Bruk den når du skal fange et helt
hendelsesforløp. Slå av igjen etterpå; den er ordrik.

**Oppstartsfangst.** De første 6 kB fra bussen bufres i RAM ved hver oppstart,
fordi det mest interessante — enumereringen — skjer før WiFi er oppe. Hentes ut
med «Dump oppstartsfangst».

Slik får du loggen ut i en fil:

```bash
esphome logs flexit-atom-lite.yaml --device <ip> > flexit.log
```

Rammene kan parses med poll/svar-validatoren i
[`research/captures/README.md`](research/captures/README.md).

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
