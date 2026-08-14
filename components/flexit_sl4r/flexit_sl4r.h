#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
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

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace esphome::flexit_sl4r {

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
// Hvor mange ganger hver kommando sendes, hver gang i sitt eget stille vindu.
//
// Vongravens original gjør nøyaktig dette: `do { ... } while (repeats<5)`.
// Grunnen er verdt å merke seg — lengdesjekken hans, `if (3 < Length <33)`,
// evalueres i C som `(3 < Length) < 33` og er dermed ALLTID sann, og han har
// samme off-by-one i header-hoppet som vi hadde. Timingen hans var altså like
// gal som vår var; han kompenserte med gjentakelse. Brute force, ikke presisjon.
static constexpr uint8_t COMMAND_REPEATS = 5;

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
static constexpr uint8_t TYPE_PARAM = 0xC7;    // IEEE754 float-parametere/grenser

// Verdi CS50 rapporterer for en følerinngang som ikke er tilkoblet.
static constexpr float SENSOR_DISCONNECTED = -55.0f;

class FlexitSL4RComponent final : public Component, public uart::UARTDevice {
#ifdef USE_SELECT
  SUB_SELECT(fan_level)
#endif
#ifdef USE_SWITCH
  SUB_SWITCH(preheat)
#endif
#ifdef USE_NUMBER
  SUB_NUMBER(heat_exchanger_setpoint)
#endif
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(preheat_active)
  SUB_BINARY_SENSOR(communication)
  SUB_BINARY_SENSOR(boost_active)
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

  // Kalt fra child-entitetene (select/switch/number) sine control()/write_state()-overrides.
  void set_fan_level(uint8_t level);                  // 1..3
  void set_preheat(bool on);
  void set_heat_exchanger_setpoint(uint8_t celsius);  // 15..25

  // Forsering («Max vifte» — dusj/matlaging). I motsetning til feltene over er
  // dette IKKE en tilstandsskriving, men en engangs-kommando: en egen kort
  // ramme som CI50 sender én gang ved knappetrykk. Fanget og sjekksum-
  // verifisert på eget anlegg 2026-08-13, se research/protocol-notes.md
  // → «Forseringskommandoen». Aggregatet går til trinn 3 og faller selv
  // tilbake til forrige trinn når perioden er over.
  void trigger_boost();

  // Dumper oppstartsfangsten til loggen. Nødvendig fordi de mest interessante
  // bytene — CS50s registrering av paneler — kommer i løpet av de første
  // sekundene etter at bussen får strøm, altså LENGE før WiFi og API er oppe
  // og vi kan lese loggen live. Komponenten mottar fra ~2 s, så vi bufrer
  // rått i RAM og henter det ut i etterkant.
  void dump_boot_capture();

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
#endif

 protected:
  // --- Mottak: generell rammeparser (erstatter den gamle snevre synk-regelen) ---
  void handle_incoming_byte_(uint8_t byte);
  void dispatch_frame_();
  void handle_float_frame_();
  void parse_and_publish_status_();

  // --- Sending: ikke-blokkerende deteksjon av CI50s kommandovindu + injeksjon ---
  // Køer en komplett, ferdig ramme (uten sjekksum — den beregnes ved sending).
  // Brukes av engangs-kommandoer som ikke passer i command_template-modellen.
  void queue_state_frame_(uint8_t fan, uint8_t flag, uint8_t setpoint);
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
  uint8_t command_repeats_left_{0};
  // Teller rammer som feilet sjekksum. Disse forkastes stille i parseren (en
  // 0xC3 inne i en payload treffer den grenen helt normalt), men da blir ekte
  // korrupsjon også usynlig. Uten denne telleren kan man ikke måle om VÅR
  // sending ødelegger CS50s trafikk — og nettopp det spørsmålet er åpent.
  uint32_t frames_discarded_{0};

  // Rå bytefangst fra oppstart. Fylles én gang, stopper når den er full.
  static constexpr size_t BOOT_CAPTURE_MAX = 6144;
  std::vector<uint8_t> boot_capture_;
  bool communication_ok_{false};
  bool preheat_active_state_{false};  // stateful latch, se protocol-notes.md

  // Sist kjente rå verdier fra CS50 — MÅ speiles inn i utgående kommandoer
  // (se protocol-notes.md: usendte felt overskrives ellers utilsiktet).
  uint8_t last_raw_fan_level_{17};
  uint8_t last_raw_preheat_{0};
  uint8_t last_raw_heat_exchanger_temp_{20};

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
  std::array<uint8_t, 5> poll_window_{};
  void send_poll_response_();

  GPIOPin *flow_control_pin_{nullptr};
};

}  // namespace esphome::flexit_sl4r
