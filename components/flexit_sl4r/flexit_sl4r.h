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
static constexpr uint32_t COMMAND_INJECT_DELAY_MS = 10;

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

 protected:
  // --- Mottak: ikke-blokkerende synk + parsing av CS50s statustelegram ---
  void handle_incoming_byte_(uint8_t byte);
  void on_status_sync_matched_(uint8_t sync_193, uint8_t sync_gap);
  void handle_status_payload_byte_(uint8_t byte);
  void parse_and_publish_status_();

  // --- Sending: ikke-blokkerende deteksjon av CI50s kommandovindu + injeksjon ---
  void handle_command_slot_byte_(uint8_t byte);
  void queue_command_(uint8_t field_offset, uint8_t value);
  void build_and_send_command_();

  static std::pair<uint8_t, uint8_t> checksum_(const uint8_t *data, size_t len);

  // Rullende historikk over de siste 9 rå byte (index 8 = nyeste), brukt til
  // mønstergjenkjenning for BÅDE status-synk og CI50-kommandovindu-start.
  std::array<uint8_t, 9> sync_history_{};

  // Status-mottak
  bool capturing_status_{false};
  uint8_t status_bytes_received_{0};
  std::array<uint8_t, STATUS_RAW_LENGTH> raw_status_{};
  uint8_t status_sync_193_{0};
  uint8_t status_sync_gap_{0};
  uint32_t last_valid_telegram_ms_{0};
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

  // CI50-kommandovindu-deteksjon (kun aktiv når command_pending_ er satt).
  enum class CmdSlotState : uint8_t {
    IDLE,
    SKIPPING_HEADER,
    READING_LENGTH,
    SKIPPING_PAYLOAD,
  } cmd_slot_state_{CmdSlotState::IDLE};
  uint8_t cmd_slot_skip_remaining_{0};

  GPIOPin *flow_control_pin_{nullptr};
};

}  // namespace esphome::flexit_sl4r
