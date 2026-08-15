#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace esphome::flexit_sl4r {

#ifdef USE_CLIMATE
// Forward-erklæring: climate-plattformen inkluderer denne headeren, så vi kan
// ikke inkludere dens header her uten sirkulær avhengighet.
class FlexitClimate;
#endif

// Se research/protocol-notes.md for utledning av alle offsets/lengder under.
static constexpr uint8_t STATUS_DATA_LENGTH = 22;   // databyte i statustelegrammet (uten sync-header/checksum)
static constexpr uint8_t STATUS_RAW_LENGTH = 25;    // total byte lest etter synk-treff (data + 2 checksum + 1 ubrukt)
static constexpr uint32_t COMMUNICATION_TIMEOUT_MS = 5000;
// Hvor lenge bussen må ha vært HELT stille før vi tør sende.
//
// Første ekte sendeforsøk 2026-08-14 kolliderte: vi sendte 10 ms etter at en
// ramme var ferdig validert, men CS50 hadde da allerede begynt på neste
// telegram. «Rett etter en ramme» er altså ikke et hull. Vi venter derfor på
// målt stillhet i stedet. 5 ms ≈ 10 tegntider ved 19200 baud, og observerte
// opphold mellom telegrammer er 20–55 ms, så det er god margin.
static constexpr uint32_t BUS_IDLE_BEFORE_TX_MS = 5;
// Gi opp en køet kommando som aldri finner et hull, i stedet for å la den
// ligge og vente i det uendelige.
static constexpr uint32_t COMMAND_GIVE_UP_MS = 5000;
// Hvor lenge vi kan være upollet før vi regner oss som droppet fra bussen.
// Observert pollintervall til oss er ~0,2 s, så 30 s er svært romslig.
static constexpr uint32_t ENUMERATION_TIMEOUT_MS = 30000;
// --- Generell rammestruktur (målt 2026-08-13, se research/protocol-notes.md) ---
//   C3 b1 b2 b3 b4 TYPE b6 LEN [LEN databyte] CK1 CK2
// Sjekksumvindu er [5 .. 8+LEN). Validert mot 23 708 avlyttede byte: 766 rammer,
// null falske C3-treff. Lengde+sjekksum er derfor en trygg rammedetektor.
static constexpr uint8_t FRAME_START = 0xC3;
static constexpr uint8_t FRAME_HEADER_LENGTH = 8;   // C3 + 4 sig + TYPE + b6 + LEN
static constexpr uint8_t FRAME_LEN_OFFSET = 7;
static constexpr uint8_t FRAME_CHECKSUM_START = 5;  // sjekksummen dekker fra TYPE
static constexpr uint8_t FRAME_MAX_PAYLOAD = 64;    // observert maks er 30

static constexpr uint8_t TYPE_STATUS = 0xC1;   // med LEN=22 er dette statustelegrammet
static constexpr uint8_t TYPE_FLOAT = 0xC2;    // IEEE754 float-registre (målinger)
static constexpr uint8_t TYPE_INT = 0xC6;      // 16-bit heltall: parametertabellene + ur-lagringen (bank 0x21)
static constexpr uint8_t TYPE_PARAM = 0xC7;    // IEEE754 float-parametere/grenser

// Verdi CS50 rapporterer for en følerinngang som ikke er tilkoblet.
static constexpr float SENSOR_DISCONNECTED = -55.0f;

// --- payload[6]: TO uavhengige bit, avklart ved styrt forsøk 2026-08-15 ---
//   bit0 (0x01) = FORSERING aktiv
//   bit7 (0x80) = ETTERVARME AKTIVERT
//
// Tre feiltolkninger gikk forut for dette, alle fordi de to bitene ble sett
// samtidig uten å variere én om gangen:
//   1. «bit0 = elementet varmer nå» — avkreftet: bit0 var satt under forsering
//      mens ettervarmen beviselig var av. Et avslått element varmer ikke.
//   2. «bit7 = ettervarme DEAKTIVERT (invertert)» — avkreftet: å slå PÅ
//      ettervarmen setter biten.
//   3. «bit7 er ikke enable-flagget i det hele tatt» — også feil. Den så ut
//      til å opptre i begge tilstander fordi VÅRE EGNE skrivinger slo av
//      ettervarmen bak ryggen på oss (se speilingsfeilene under).
// Verifisert begge veier: aktiver -> 0x80, deaktiver -> 0x00, forsering -> 0x01.
// Vongravens opprinnelige «0=av, 128=på» var altså riktig hele tiden.
static constexpr uint8_t STATUS_BOOST_ACTIVE = 0x01;
static constexpr uint8_t STATUS_AFTERHEAT_ENABLED = 0x80;

// Samme flagg i PANELETS tilstandsramme (`data[4]` i node 4-rammen `20 0F`),
// som er formatet vi selv skriver i. Bit6 er en kortvarig knappebit.
static constexpr uint8_t AFTERHEAT_ENABLED_BIT = 0x80;
static constexpr uint8_t PANEL_BUTTON_BIT = 0x40;

// --- payload[4]: alarmbitfelt (MÅLT 2026-08-14) ---
// Gikk fra 2 til 0 i det brukeren nullstilte filteralarmen på panelet, og ble
// værende 0. Andre bit er ikke observert — rotoralarm og overhetingstermostat
// er sannsynlige kandidater.
static constexpr uint8_t ALARM_FILTER = 0x02;

// --- payload[2] bit0: varmegjenvinneren går (MÅLT 2026-08-15) ---
// Settes IKKE når varmepådraget starter, men når det passerer ~10 — altså når
// rotoren faktisk begynner å snurre, ikke når behovet oppstår. Verifisert på
// både stigende og fallende flanke: `[11]` 0→68 satte biten ved 10, og da
// pådraget falt tilbake til 0 ble den klarert.
static constexpr uint8_t HEAT_RECOVERY_RUNNING = 0x01;
// Bit1 i samme gruppe. ALDRI observert satt i 837 statustelegram. Flexit bruker
// samme utgang (J5 pin 11,12) til «rotor ELLER bypass motor» avhengig av
// aggregattype, så tolkningen er en KVALIFISERT GJETNING: på et
// plateveksleraggregat koder gruppa trolig bypass-tilstand. Kan ikke avgjøres
// på et rotoraggregat. Vippes den noen gang, fanges den som anomali.
static constexpr uint8_t HEAT_RECOVERY_BYPASS = 0x02;

// --- data[4] i tilstandsrammen: knappehendelser (MÅLT 2026-08-14) ---
// Feltet vi lenge førte som «uavklart». Det ser ut til å rapportere hvilke
// panelknapper som er trykket:
//   0x01  — forseringsknappen (sett rett før en forseringskommando)
//   0xC0  — begge temperaturknappene samtidig (= filterreset-bevegelsen)
// Normalt 0x00.
static constexpr uint8_t BUTTON_NONE = 0x00;
// Manualens påkrevde settpunkt under filterreset.
static constexpr uint8_t FILTER_RESET_SETPOINT = 20;

class FlexitSL4RComponent final : public Component, public uart::UARTDevice {
#ifdef USE_SELECT
  SUB_SELECT(fan_level)
#endif
#ifdef USE_NUMBER
  SUB_NUMBER(heat_exchanger_setpoint)
#endif
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(afterheat_active)    // payload[6] bit0 — forsering (uavhengig kryssjekk mot [5]-niblene)
  SUB_BINARY_SENSOR(afterheat_enabled)   // payload[6] bit7 — ettervarme aktivert
  SUB_BINARY_SENSOR(filter_alarm)        // payload[4] bit1 — filtertid utløpt
  // Ethvert annet bit i alarmfeltet enn filterbiten. Fanger den røde
  // alarm-LED-en (rotoralarm / overhetingstermostat — begge udokumenterte
  // bits) den dagen den fyrer. HVILKET bit leses av «Rå status[4]».
  SUB_BINARY_SENSOR(unknown_alarm)
  SUB_BINARY_SENSOR(heat_recovery_active)  // payload[2] bit0 — rotoren går
  SUB_BINARY_SENSOR(bypass_active)         // payload[2] bit1 — ANTATT bypass, aldri observert
  SUB_BINARY_SENSOR(communication)
  SUB_BINARY_SENSOR(boost_active)
  // Blir vi faktisk pollet? Enumereringen skjer KUN når CS50 starter opp. Er
  // noden vår nede da, droppes vi — og siden skriving skjer som poll-svar,
  // feiler den da STILLE. Uten dette signalet ser alt normalt ut mens ingen
  // kommandoer kommer fram.
  SUB_BINARY_SENSOR(enumerated)
#endif
#ifdef USE_SENSOR
  SUB_SENSOR(supply_air_temperature)       // 0xC2 reg 0 slot 1 — Flexits B1
  SUB_SENSOR(heat_exchanger_setpoint_raw)  // 0xC2 reg 7 slot 1 — settpunkt som float
  SUB_SENSOR(fan_duty_supply)              // status payload[13], %
  SUB_SENSOR(fan_duty_extract)             // status payload[14], %
  SUB_SENSOR(fan_level_running)            // høy nibbel av payload[5]
  SUB_SENSOR(fan_level_return)             // lav nibbel — trinnet forseringen faller tilbake til
  SUB_SENSOR(frames_discarded)             // rammer forkastet på sjekksum — gjør busskorrupsjon målbar
  SUB_SENSOR(status_interval)              // sekunder mellom to statustelegram — se under
  SUB_SENSOR(heat_demand)                  // payload[11] — varmepådrag, driver rotoren
  SUB_SENSOR(anomalies)
#endif
#ifdef USE_TEXT_SENSOR
  // Firmwareversjoner. CS50 sender sin i bank 0x20 reg 0x00, panelet sin i
  // bank 0x22 reg 0x00 — begge som 8 byte ASCII. Tilsvarer manualens
  // «Test → Informasjon → Main board / CS50 panel 1: Programvare rev.»
  SUB_TEXT_SENSOR(controller_firmware)
  SUB_TEXT_SENSOR(panel_firmware)
#endif

 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }
  // Hvilken node vi utgir oss for å være når vi sender. CI50 er node 4
  // (panel 1). Node 5 er panel 2 — jf. dipswitch 3 på panelet. Adressefeltet
  // beregnes av set_source_node_, se der.
  void set_source_node(uint8_t node) { this->source_node_ = node; }
  // Svar på poll adressert til vår node. Bussen er POLLED (målt 2026-08-14):
  // masteren sender 5 byte `C3 <node> 00 <cks>`, og KUN den adresserte noden
  // svarer — uten å gjenta C3-headeren. Noder som ikke svarer på
  // enumereringsskanningen ved oppstart blir droppet.
  void set_respond_to_polls(bool v) { this->respond_to_polls_ = v; }

#ifdef USE_CLIMATE
  void set_ventilation_climate(FlexitClimate *c) { this->ventilation_climate_ = c; }
#endif

  // Kalt fra child-entitetene (select/switch/number) sine control()/write_state()-overrides.
  void set_fan_level(uint8_t level);                  // 1..3
  void set_heat_exchanger_setpoint(uint8_t celsius);  // 15..25

  // Forsering («Max vifte» — dusj/matlaging). I motsetning til feltene over er
  // dette IKKE en tilstandsskriving, men en engangs-kommando: en egen kort
  // ramme som CI50 sender én gang ved knappetrykk. Fanget og sjekksum-
  // verifisert på eget anlegg 2026-08-13, se research/protocol-notes.md
  // → «Forseringskommandoen». Aggregatet går til trinn 3 og faller selv
  // tilbake til forrige trinn når perioden er over.
  void trigger_boost();

  // Avbryter forseringen ved å skrive returtrinnet — samme mekanisme som en
  // manuell trinn-endring, bare med trinnet aggregatet uansett skal tilbake til.
  void cancel_boost();

  // Nullstiller filtervakttimeren, med hele prosedyren fra CI 50-manualen:
  // settpunkt til 20 grader, så reset-flagget, så tilbake til opprinnelig
  // settpunkt. Manualen krever 20-graders-steget, og en reset uten det viste
  // seg å kvittere alarmen midlertidig UTEN å restarte timeren — den kom
  // tilbake av seg selv. Se research/protocol-notes.md.
  void reset_filter_timer();
  // Slår ettervarmen av eller på ved å skrive feltet panelet selv bruker.
  void set_afterheat_enabled(bool on);
  bool get_afterheat_enabled() const { return this->afterheat_enabled_; }

  // --- EKSPERIMENT: skriving til parameterregistrene (bank 0x20, type 0xC6) ---
  //
  // FARE. `0xC6`-blokkene er aggregatets DRIFTSPARAMETERE og ligger etter alt
  // å dømme i EEPROM. En feilskriving kan ikke nødvendigvis nullstilles med en
  // strømsykling. Derfor:
  //   * dobbel gating i runtime (egen «Eksperimentmodus»-bryter i tillegg til
  //     knappen), slik at et feiltrykk alene ikke gjør noe,
  //   * målet er ETT ord som er en ren visningsgrense — maks settpunkt — og som
  //     kan leses tilbake fra kringkastingsrunden innen ett sekund,
  //   * hele blokken speiles fra siste mottatte ramme, og KUN én byte endres,
  //     så selv om CS50 skulle skrive alle 14 ordene, skrives de tilbake til
  //     nøyaktig de verdiene de allerede hadde.
  //
  // Er `enabled` false gjør kallet ingenting og logger en advarsel.
  void set_param_write_enabled(bool on);
  bool get_param_write_enabled() const { return this->param_write_enabled_; }
  // Skriver maks settpunkt (bank 0x20 reg 0x00, ord 0, lav byte).
  void write_max_setpoint_test(uint8_t celsius);

  // Dumper oppstartsfangsten til loggen. Nødvendig fordi de mest interessante
  // bytene — CS50s registrering av paneler — kommer i løpet av de første
  // sekundene etter at bussen får strøm, altså LENGE før WiFi og API er oppe
  // og vi kan lese loggen live. Komponenten mottar fra ~2 s, så vi bufrer
  // rått i RAM og henter det ut i etterkant.
  void dump_boot_capture();

  // --- Anomalifangst ---
  // Logger det UVENTEDE, ikke alt: nye rammetyper, endringer i felt vi tror er
  // konstante, og enhver endring i alarmfeltet. Da koster det nesten ingenting
  // å stå påslått permanent, og vi har full kontekst den dagen en alarm går
  // eller noe annet uforutsett skjer.
  void dump_anomalies();
  // Rå hex-logging av hver validerte ramme, skrudd av/på i drift uten reflash.
  void set_raw_logging(bool on) {
    this->raw_logging_ = on;
    ESP_LOGI("flexit_sl4r", "Rå rammelogging %s", on ? "PÅ" : "AV");
  }
  bool get_raw_logging() const { return this->raw_logging_; }

#ifdef USE_SENSOR
  // Generiske «utforsknings»-sensorer. Poenget er å kunne eksponere hvilken som
  // helst byte eller flyttall-slot i HA UTEN å endre C++-koden, slik at
  // recorder-en bygger historikk vi kan korrelere mot senere. Det er billigere
  // enn å ta nye uart-debug-opptak hver gang en hypotese skal prøves.
  void add_raw_status_sensor(uint8_t index, sensor::Sensor *sensor) {
    this->raw_status_sensors_.emplace_back(index, sensor);
  }
  void add_float_register_sensor(uint8_t type, uint8_t reg, uint8_t slot, sensor::Sensor *sensor) {
    this->float_register_sensors_.push_back({type, reg, slot, sensor});
  }
  // 16-bit-ord fra 0xC6-rammene (parametertabellene). `index` er ordindeks
  // innenfor rammen (0–13); `mode` velger hele ordet eller én av bytene —
  // flere parametre er lagret som (min, maks)-BYTEPAR i ett ord.
  // Publiserer kun ved endring: rammene gjentas kontinuerlig i CS50s runde,
  // og verdiene er parametre/tellere som nesten aldri endrer seg.
  void add_int_register_sensor(uint8_t bank, uint8_t reg, uint8_t index, uint8_t mode, sensor::Sensor *sensor) {
    this->int_register_sensors_.push_back({bank, reg, index, mode, sensor, -1});
  }
#endif

 protected:
  // --- Mottak: generell rammeparser (erstatter den gamle snevre synk-regelen) ---
  void handle_incoming_byte_(uint8_t byte);
  void dispatch_frame_();
  void handle_float_frame_();
  void handle_int_frame_();
  void parse_and_publish_status_();

  // --- Sending: ikke-blokkerende deteksjon av CI50s kommandovindu + injeksjon ---
  // Køer en komplett, ferdig ramme (uten sjekksum — den beregnes ved sending).
  // Brukes av engangs-kommandoer som ikke passer i command_template-modellen.
  void queue_state_frame_(uint8_t fan, uint8_t flag, uint8_t setpoint);
  void handle_panel_frame_();
  void publish_firmware_(bool controller);
  void queue_raw_frame_(std::vector<uint8_t> frame_without_checksum, uint8_t repeats = 1);
  void send_queued_frame_();

  static std::pair<uint8_t, uint8_t> checksum_(const uint8_t *data, size_t len);

  // --- Rammeoppsamling ---
  // Vi samler fra hver 0xC3 og validerer med lengde + sjekksum. Slår en ramme
  // feil, forkastes den og vi leter etter neste 0xC3 — en 0xC3 inne i en payload
  // gir da bare ett bortkastet forsøk, ikke varig desynkronisering.
  bool collecting_frame_{false};
  uint8_t frame_expected_{0};  // 0 = lengden er ikke lest ennå
  std::vector<uint8_t> frame_;

  // Status-mottak (fylles fra rammeparseren for TYPE_STATUS med LEN=22)
  std::array<uint8_t, STATUS_RAW_LENGTH> raw_status_{};
  uint8_t status_sync_193_{0};
  uint8_t status_sync_gap_{0};
  // Helsesignalet hviler på HVILKEN SOM HELST gyldig ramme, ikke bare
  // statustelegrammet. Statustelegrammet (`C1`/`len=22`) er én av mange
  // meldingstyper på bussen, og kommer sjeldnere enn resten — å binde
  // «Kommunikasjon OK» til nettopp den gjorde at en fullt frisk node
  // rapporterte `off`. Rammer valideres med lengde + sjekksum, så en gyldig
  // ramme av hvilken som helst type er et sterkt livstegn.
  uint32_t last_valid_frame_ms_{0};
  uint32_t last_valid_telegram_ms_{0};
  uint32_t last_rx_byte_ms_{0};    // for deteksjon av stille buss før sending
  uint32_t command_queued_ms_{0};  // for å gi opp en kommando som aldri får plass
  // Teller rammer som feilet sjekksum. Disse forkastes stille i parseren (en
  // 0xC3 inne i en payload treffer den grenen helt normalt), men da blir ekte
  // korrupsjon også usynlig. Uten denne telleren kan man ikke måle om VÅR
  // sending ødelegger CS50s trafikk — og nettopp det spørsmålet er åpent.
  uint32_t frames_discarded_{0};

  // Rå bytefangst fra oppstart. Fylles én gang, stopper når den er full.
  static constexpr size_t BOOT_CAPTURE_MAX = 6144;
  std::vector<uint8_t> boot_capture_;

  // --- Anomalifangst ---
  static constexpr size_t ANOMALY_MAX = 40;
  // Læringsperiode: de første sekundene etter oppstart lærer noden anleggets
  // normale repertoar av rammetyper uten å melde fra. Etterpå er enhver ny
  // signatur en ekte hendelse. Bedre enn å telle et vilkårlig antall — hvor
  // mange typer et anlegg sender varierer med utrustningen.
  static constexpr uint32_t ANOMALY_LEARN_MS = 30000;
  struct Anomaly {
    uint32_t ms;
    const char *reason;
    std::vector<uint8_t> frame;
  };
  std::vector<Anomaly> anomalies_;
  uint32_t anomaly_count_{0};
  bool raw_logging_{false};
  // Rammesignaturer vi har sett før: (TYPE<<16) | (LEN<<8) | bank.
  std::vector<uint32_t> seen_signatures_;
  // Forrige statustelegram, for å oppdage endringer i «konstante» felt.
  std::array<uint8_t, STATUS_DATA_LENGTH> prev_status_{};
  bool have_prev_status_{false};
  void note_anomaly_(const char *reason);
  bool communication_ok_{false};

  // Sist kjente rå verdier fra CS50 — MÅ speiles inn i utgående kommandoer
  // (se protocol-notes.md: usendte felt overskrives ellers utilsiktet).
  uint8_t last_raw_fan_level_{17};
  uint8_t last_raw_afterheat_{0};
  // Ettervarmens av/på. Leses fra STATUSTELEGRAMMET ([6] bit7), som kringkastes
  // kontinuerlig — ikke fra panelrammen, som bare sendes ved endring og derfor
  // kan være timer gammel. Det siste var en reell feilkilde: etter en omstart
  // sto feltet på `false` til panelet tilfeldigvis sendte noe, og siden verdien
  // speiles inn i ALLE våre skrivinger, slo den første viftekommandoen etter
  // omstart av ettervarmen bak brukerens rygg.
  bool afterheat_enabled_{false};
  bool have_afterheat_state_{false};
  // PRINSIPP: alt vi ikke forstår speiles fra panelets siste ramme, slik at en
  // skriving kun endrer det vi FAKTISK mener å endre. Vi hardkodet felt vi
  // trodde var konstante, og slo dermed av ettervarmen ved hver skriving.
  std::array<uint8_t, 8> panel_state_{{0x20, 0x0F, 0x00, 0x11, 0x00, 0x04, 0x00, 0x12}};
  bool have_panel_state_{false};
  uint8_t last_raw_heat_exchanger_temp_{20};
  // Tilluft, speilet fra 0xC2 reg 0 slot 1 — climate-entiteten trenger den som
  // «current temperature», og den kommer i en annen ramme enn statusen.
  float last_supply_air_temp_{NAN};
  // Siste mottatte C6 bank 0x20 reg 0x00-blokk, speilet for skriveeksperimentet.
  std::vector<uint8_t> last_param_block_;
  bool param_write_enabled_{false};
#ifdef USE_CLIMATE
  FlexitClimate *ventilation_climate_{nullptr};
  void publish_climate_();
#endif

  // Kommandokø: kun én utestående kommando av gangen, nyeste vinner.
  // FIFO-kø av ferdige rammer (uten sjekksum — den påføres ved sending).
  // Én ramme sendes per stille vindu, slik at en sekvens av rammer legges ut
  // på bussen i riktig rekkefølge med reell arbitrering mellom hver.
  // Nødvendig fordi CI50 sender TO rammer ved et forseringstrykk, ikke én.
  struct QueuedFrame {
    std::vector<uint8_t> bytes;
    uint8_t repeats;
  };
  std::vector<QueuedFrame> tx_queue_;


#ifdef USE_SENSOR
  std::vector<std::pair<uint8_t, sensor::Sensor *>> raw_status_sensors_;
  struct FloatRegisterSensor {
    uint8_t type;
    uint8_t reg;
    uint8_t slot;
    sensor::Sensor *sensor;
  };
  std::vector<FloatRegisterSensor> float_register_sensors_;
  struct IntRegisterSensor {
    uint8_t bank;
    uint8_t reg;
    uint8_t index;  // ordindeks i rammen, 0-13
    uint8_t mode;   // 0 = helt ord, 1 = høy byte, 2 = lav byte
    sensor::Sensor *sensor;
    int32_t last_value;  // -1 = aldri publisert (alle reelle verdier er 0-65535)
  };
  std::vector<IntRegisterSensor> int_register_sensors_;
#endif

  // Bygger de fem headerbytene for vår avsenderadresse. Byte 3-4 er
  // Fletcher-sjekksummen over [C3, node, 00] — verifisert eksakt mot node 1
  // (`01 00 C4 4B`) og node 4 (`04 00 C7 51`) i avlyttet trafikk.
  std::array<uint8_t, 5> source_header_() const {
    const uint8_t hdr[3] = {FRAME_START, this->source_node_, 0x00};
    const auto [s1, s2] = checksum_(hdr, 3);
    return {FRAME_START, this->source_node_, 0x00, s1, s2};
  }
  uint8_t source_node_{4};
  bool respond_to_polls_{false};
  uint32_t last_poll_to_us_ms_{0};
  bool enumerated_{false};
  std::array<uint8_t, 5> poll_window_{};
  void send_poll_response_();

  GPIOPin *flow_control_pin_{nullptr};
};

}  // namespace esphome::flexit_sl4r
