# Flexit SL4R (CS50/CI50) — ESPHome-integrasjon via RS485

Toveis integrasjon av et Flexit SL4R-ventilasjonsaggregat (CS50-hovedkort,
CI50-betjeningspanel) i Home Assistant, uten Flexit sin egen CI66-adapter.
Leser status (viftetrinn, forvarme, settpunkt varmeveksler) og — når Fase 2
er verifisert med maskinvare — sender kommandoer tilbake.

Maskinvare: M5Stack **ATOM Lite** (ESP32) + **ATOM Tail485** (TTL↔RS485),
koblet i parallell med CI50 på en ledig RJ10/RJ11-port på CS50-kortet.

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
research/                 Kildemateriale + protokollutledning
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
2. Når Tail485 ankommer: **mål spenning på CS50s ledige RJ10/RJ11-port med
   multimeter FØR tilkobling** (avgjør om Tail485 kan strømforsynes fra
   bussen, se protocol-notes.md punkt 4).
3. Koble Tail485 i parallell med CI50. Kjør Fase 1 (kun lytting): verifiser
   at statustelegrammer synkroniseres og sjekksummer stemmer i loggen.
4. Avlytt et ekte CI50→CS50-kommandotelegram (endre f.eks. viftetrinn på
   selve panelet mens ESP-en logger rå bytes) og sammenlign mot
   `command_template` i `flexit-atom-lite.yaml` — korriger malen ved avvik.
5. Slå på Fase 2: legg til `select`/`switch`/`number`-entitetene og test
   sending, bekreft at CI50-panelet viser endringene og at ingen andre
   verdier endres utilsiktet.
6. Når stabilt: kopier `components/` og `flexit-atom-lite.yaml` inn i
   `/config/esphome/` på Home Assistant-verten (egen nested git der, se
   `CLAUDE.md` i homeassistant-workspace).
