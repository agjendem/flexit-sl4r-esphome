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
static constexpr uint8_t COMMAND_LENGTH = 18;        // total lengde på kommandotelegrammet til CS50
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
#endif

 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_command_template(const std::vector<uint8_t> &command_template) {
    this->command_template_ = command_template;
  }
  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }

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
  void queue_command_(uint8_t field_offset, uint8_t value);
  // Køer en komplett, ferdig ramme (uten sjekksum — den beregnes ved sending).
  // Brukes av engangs-kommandoer som ikke passer i command_template-modellen.
  void queue_raw_frame_(std::vector<uint8_t> frame_without_checksum);
  void build_and_send_command_();

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
  uint32_t last_valid_telegram_ms_{0};
  uint32_t last_rx_byte_ms_{0};    // for deteksjon av stille buss før sending
  uint32_t command_queued_ms_{0};  // for å gi opp en kommando som aldri får plass
  uint8_t command_repeats_left_{0};
  bool communication_ok_{false};
  bool preheat_active_state_{false};  // stateful latch, se protocol-notes.md

  // Sist kjente rå verdier fra CS50 — MÅ speiles inn i utgående kommandoer
  // (se protocol-notes.md: usendte felt overskrives ellers utilsiktet).
  uint8_t last_raw_fan_level_{17};
  uint8_t last_raw_preheat_{0};
  uint8_t last_raw_heat_exchanger_temp_{20};

  // Kommandokø: kun én utestående kommando av gangen, nyeste vinner.
  bool command_pending_{false};
  uint8_t pending_field_offset_{0};
  uint8_t pending_field_value_{0};
  std::vector<uint8_t> command_template_;
  // Ferdig ramme som skal sendes i stedet for command_template-varianten.
  // Tom = bruk den vanlige feltskrivingen.
  std::vector<uint8_t> pending_raw_frame_;


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

  GPIOPin *flow_control_pin_{nullptr};
};

}  // namespace esphome::flexit_sl4r
