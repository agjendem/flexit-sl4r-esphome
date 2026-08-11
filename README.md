# Flexit SL4R (CS50/CI50) — ESPHome-integrasjon via RS485

Toveis integrasjon av et Flexit SL4R-ventilasjonsaggregat (CS50-hovedkort,
CI50-betjeningspanel) i Home Assistant, uten Flexit sin egen CI66-adapter.
Leser status (viftetrinn, forvarme, settpunkt varmeveksler) og — når Fase 2
er verifisert med maskinvare — sender kommandoer tilbake.

Maskinvare: M5Stack **ATOM Lite** (ESP32) + **ATOM Tail485** (TTL↔RS485,
SKU T002), koblet i parallell med CI50 på en ledig **4P4C**-port på
CS50-kortet. Pinout og strømforsyning: se
[`research/protocol-notes.md`](research/protocol-notes.md) → «Fysisk tilkobling».

## Status

- **Fase 1 (lytting):** komponent skrevet og kompilert rent mot ESP32
  (Arduino-rammeverk), men **ikke testet mot ekte maskinvare** — Tail485
  ankommer om noen uker. All protokollkunnskap er reverse-engineered fra
  [Vongraven/Flexit-SL4R-master](https://github.com/Vongraven/Flexit-SL4R-master)
  (Arduino Mega, testet på ekte SL4R/CS50) og verifisert numerisk mot
  README-eksemplene der (sjekksumalgoritme stemmer eksakt). Se
  [`research/protocol-notes.md`](research/protocol-notes.md) for full
  utledning.
- **Fase 2 (sending):** kode skrevet (ikke-blokkerende kommandovindu-
  deteksjon + injeksjon), men kommandomalen inneholder foreløpig ukjente
  byte kopiert fra Vongravens eksempel og MÅ verifiseres mot avlyttet
  CI50-trafikk på vårt eget anlegg før den tas i bruk. Sending er derfor
  IKKE koblet inn i `flexit-atom-lite.yaml` som standard.

## Repo-struktur

```
components/flexit_sl4r/   ESPHome external_component (C++ hub + select/switch/number/binary_sensor)
research/                 Kildemateriale + protokollutledning (se research/README.md)
flexit-atom-lite.yaml     Eksempel-/produksjonskonfig for ATOM Lite + Tail485
secrets.yaml.example      Mal for secrets.yaml (wifi/api/ota)
```

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

1. Flash ATOM Lite når den ankommer (dag/to) — verifiser boot, wifi, API mot
   HA uten RS485 tilkoblet ennå.
2. Når Tail485 ankommer: **mål pinne 4 mot pinne 1 på CS50s ledige 4P4C-port
   med multimeter FØR tilkobling**. Tail485 sin DC/DC krever 9–24 V inn;
   ~12 V er forventet. Måler den 12 V kan hele oppsettet mates fra bussen
   (én kabel, ingen egen strømforsyning); måler den 5 V er det for lavt for
   omformeren, og da må ATOM-en mates via USB-C med klemme V ukoblet.
   Se protocol-notes.md → «Fysisk tilkobling».
3. Koble Tail485 i parallell med CI50 (enten i den ledige porten på kortet
   eller i den ledige kontakten bak på selve CI50-panelet — elektrisk samme
   buss, og panelet er ofte lettere tilgjengelig). Kjør Fase 1 (kun lytting):
   verifiser at statustelegrammer synkroniseres og sjekksummer stemmer i
   loggen. Er det helt stille: bytt om A og B før du mistenker koden.
4. Avlytt et ekte CI50→CS50-kommandotelegram (endre f.eks. viftetrinn på
   selve panelet mens ESP-en logger rå bytes) og sammenlign mot
   `command_template` i `flexit-atom-lite.yaml` — korriger malen ved avvik.
5. Slå på Fase 2: legg til `select`/`switch`/`number`-entitetene og test
   sending, bekreft at CI50-panelet viser endringene og at ingen andre
   verdier endres utilsiktet.
6. Når stabilt: kopier `components/` og `flexit-atom-lite.yaml` inn i
   `/config/esphome/` på Home Assistant-verten (egen nested git der, se
   `CLAUDE.md` i homeassistant-workspace).

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
