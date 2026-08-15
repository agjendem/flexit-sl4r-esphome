#include "flexit_sl4r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#ifdef USE_CLIMATE
#include "climate/flexit_climate.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace esphome::flexit_sl4r {

static const char *const TAG = "flexit_sl4r";

void FlexitSL4RComponent::dump_boot_capture() {
  ESP_LOGI(TAG, "=== BOOT CAPTURE: %u bytes ===", static_cast<unsigned>(this->boot_capture_.size()));
  char line[3 * 32 + 1];
  for (size_t i = 0; i < this->boot_capture_.size(); i += 32) {
    const size_t n = std::min<size_t>(32, this->boot_capture_.size() - i);
    for (size_t k = 0; k < n; k++)
      sprintf(line + 3 * k, "%02X ", this->boot_capture_[i + k]);
    line[3 * n] = '\0';
    ESP_LOGI(TAG, "BOOT %04u: %s", static_cast<unsigned>(i), line);
  }
  ESP_LOGI(TAG, "=== END OF BOOT CAPTURE ===");
}

void FlexitSL4RComponent::setup() {
  this->boot_capture_.reserve(BOOT_CAPTURE_MAX);
  // Publish a defined initial state. Without this the entity stays `unknown`
  // until the value CHANGES - and since loop() only publishes on change, a
  // healthy node would in practice never report "OK".
#ifdef USE_BINARY_SENSOR
  if (this->communication_binary_sensor_ != nullptr)
    this->communication_binary_sensor_->publish_state(false);
  if (this->enumerated_binary_sensor_ != nullptr)
    this->enumerated_binary_sensor_->publish_state(false);
#endif
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);
  }
}

void FlexitSL4RComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Flexit SL4R (CS50/CI50 RS485):");
  LOG_PIN("  Flow control pin: ", this->flow_control_pin_);
  this->check_uart_settings(19200, 1, uart::UART_CONFIG_PARITY_NONE, 8);
#ifdef USE_SELECT
  LOG_SELECT("  ", "Fan level", this->fan_level_select_);
#endif
#ifdef USE_NUMBER
  LOG_NUMBER("  ", "Heat exchanger setpoint", this->heat_exchanger_setpoint_number_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Afterheater active", this->afterheat_active_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Communication OK", this->communication_binary_sensor_);
#endif
}

void FlexitSL4RComponent::loop() {
  while (this->available() != 0) {
    const int data = this->read();
    if (data < 0)
      break;
    this->last_rx_byte_ms_ = millis();
    if (this->boot_capture_.size() < BOOT_CAPTURE_MAX)
      this->boot_capture_.push_back(static_cast<uint8_t>(data));
    this->handle_incoming_byte_(static_cast<uint8_t>(data));
  }

  // We transmit ONLY after measured silence on the bus. Sending "right after a
  // completed frame" collided in practice, because the CS50 starts the next
  // telegram before that (see PROTOCOL.md).
  // NOTE: in poll-response mode the queue must be drained ONLY by
  // send_poll_response_(). The old "send when the bus is quiet" path otherwise
  // stole the frame and put it on the wire unsolicited - and on a polled bus
  // nobody listens then. That was why fan level did not work even with
  // byte-identical content.
  // NOTE: the guard covers the transmit block ONLY. It used to be a `return`
  // here, and since poll mode is always on it also skipped the health check
  // below - which was the entire reason "communication OK" never went `on`.
  if (!this->respond_to_polls_ && !this->tx_queue_.empty() && !this->collecting_frame_) {
    const uint32_t now = millis();
    if (now - this->last_rx_byte_ms_ >= BUS_IDLE_BEFORE_TX_MS) {
      this->send_queued_frame_();
    } else if (now - this->command_queued_ms_ > COMMAND_GIVE_UP_MS) {
      ESP_LOGW(TAG, "Never found a quiet window on the bus - discarding %u queued frame(s)",
               static_cast<unsigned>(this->tx_queue_.size()));
      this->tx_queue_.clear();
    }
  }

  // Are we still in the CS50's poll round? If this goes away, every write
  // fails silently - the unit must then be power-cycled to re-enumerate us.
  if (this->respond_to_polls_) {
    const bool enumerated =
        this->last_poll_to_us_ms_ != 0 && (millis() - this->last_poll_to_us_ms_) < ENUMERATION_TIMEOUT_MS;
    if (enumerated != this->enumerated_) {
      this->enumerated_ = enumerated;
#ifdef USE_BINARY_SENSOR
      if (this->enumerated_binary_sensor_ != nullptr)
        this->enumerated_binary_sensor_->publish_state(enumerated);
#endif
      if (!enumerated)
        ESP_LOGW(TAG, "No longer polled by the CS50 - writes will fail silently until the unit is power-cycled");
    }
  }

  const bool now_ok =
      this->last_valid_frame_ms_ != 0 && (millis() - this->last_valid_frame_ms_) < COMMUNICATION_TIMEOUT_MS;
  if (now_ok != this->communication_ok_) {
    this->communication_ok_ = now_ok;
#ifdef USE_BINARY_SENSOR
    if (this->communication_binary_sensor_ != nullptr) {
      this->communication_binary_sensor_->publish_state(now_ok);
    }
#endif
    if (!now_ok) {
      ESP_LOGW(TAG, "No valid frames in the last %u ms", static_cast<unsigned>(COMMUNICATION_TIMEOUT_MS));
    }
  }
}

void FlexitSL4RComponent::send_poll_response_() {
  // The reply is the body ONLY: <TYPE> <node> <LEN> <data...> <CK CK>.
  // The C3 header belongs to the poll and must NOT be repeated - that mistake
  // is exactly why every earlier transmit attempt was ignored.
  std::vector<uint8_t> body;
  if (!this->tx_queue_.empty()) {
    body = this->tx_queue_.front().bytes;
    if (--this->tx_queue_.front().repeats == 0)
      this->tx_queue_.erase(this->tx_queue_.begin());
  } else {
    // "Nothing to report" - the same short reply the CI50 gives between events.
    body = {0xC0, this->source_node_, 0x02, 0x22, 0x00};
  }
  const auto [s1, s2] = checksum_(body.data(), body.size());
  body.push_back(s1);
  body.push_back(s2);

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);
  this->write_array(body.data(), body.size());
  this->flush();
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);
  ESP_LOGD(TAG, "Answered poll for node %u (%u bytes)", static_cast<unsigned>(this->source_node_),
           static_cast<unsigned>(body.size()));
}

void FlexitSL4RComponent::handle_incoming_byte_(uint8_t byte) {
  // --- Poll detection ---
  // Rolling 5-byte window. When it matches the poll for OUR node, we reply at
  // once.
  if (this->respond_to_polls_) {
    for (size_t i = 0; i + 1 < this->poll_window_.size(); i++)
      this->poll_window_[i] = this->poll_window_[i + 1];
    this->poll_window_.back() = byte;
    const auto hdr = this->source_header_();
    if (std::equal(hdr.begin(), hdr.end(), this->poll_window_.begin())) {
      this->poll_window_.fill(0);
      this->collecting_frame_ = false;
      this->last_poll_to_us_ms_ = millis();
      this->send_poll_response_();
      return;
    }
  }

  // --- General frame assembly ---
  // NOTE: the old separate "command window" state machine has been removed. It
  // consumed bytes outside the frame parser to find a gap to transmit in, and
  // had two bugs that together disabled reception entirely. The frame parser
  // already knows exactly when a telegram ends - that IS the gap - so
  // arbitration needs no byte reading of its own.
  if (!this->collecting_frame_) {
    if (byte != FRAME_START)
      return;
    this->collecting_frame_ = true;
    this->frame_expected_ = 0;
    this->frame_.clear();
  }

  this->frame_.push_back(byte);

  // The length byte sits at offset 7 and determines how much more to collect.
  if (this->frame_expected_ == 0 && this->frame_.size() == FRAME_HEADER_LENGTH) {
    const uint8_t len = this->frame_[FRAME_LEN_OFFSET];
    if (len > FRAME_MAX_PAYLOAD) {
      // Implausible length: this was a 0xC3 inside a payload, not a frame.
      this->collecting_frame_ = false;
      return;
    }
    this->frame_expected_ = FRAME_HEADER_LENGTH + len + 2;  // + CK1 CK2
  }

  if (this->frame_expected_ == 0 || this->frame_.size() < this->frame_expected_)
    return;

  this->collecting_frame_ = false;

  const size_t data_end = this->frame_.size() - 2;
  const auto [sum1, sum2] = checksum_(this->frame_.data() + FRAME_CHECKSUM_START, data_end - FRAME_CHECKSUM_START);
  if (sum1 != this->frame_[data_end] || sum2 != this->frame_[data_end + 1]) {
    // Discarded silently in the log - a 0xC3 inside a payload hits this branch
    // quite normally - but COUNTED, so that real corruption is measurable.
    // Without the counter a damaged bus is invisible, and you cannot tell
    // whether our own transmissions are colliding with the CS50.
    this->frames_discarded_++;
#ifdef USE_SENSOR
    if (this->frames_discarded_sensor_ != nullptr)
      this->frames_discarded_sensor_->publish_state(this->frames_discarded_);
#endif
    return;
  }

  this->last_valid_frame_ms_ = millis();
  this->dispatch_frame_();
}

void FlexitSL4RComponent::note_anomaly_(const char *reason) {
  this->anomaly_count_++;
  if (this->anomalies_.size() >= ANOMALY_MAX)
    this->anomalies_.erase(this->anomalies_.begin());
  this->anomalies_.push_back({millis(), reason, this->frame_});
#ifdef USE_SENSOR
  if (this->anomalies_sensor_ != nullptr)
    this->anomalies_sensor_->publish_state(this->anomaly_count_);
#endif
  // Also logged immediately, so it is captured by an ordinary log stream.
  char line[3 * 40 + 1];
  const size_t n = std::min<size_t>(40, this->frame_.size());
  for (size_t k = 0; k < n; k++)
    sprintf(line + 3 * k, "%02X ", this->frame_[k]);
  line[3 * n] = '\0';
  ESP_LOGW(TAG, "ANOMALY (%s): %s", reason, line);
}

void FlexitSL4RComponent::dump_anomalies() {
  ESP_LOGI(TAG, "=== ANOMALIES: %u stored, %u total since boot ===",
           static_cast<unsigned>(this->anomalies_.size()), static_cast<unsigned>(this->anomaly_count_));
  char line[3 * 40 + 1];
  for (const auto &a : this->anomalies_) {
    const size_t n = std::min<size_t>(40, a.frame.size());
    for (size_t k = 0; k < n; k++)
      sprintf(line + 3 * k, "%02X ", a.frame[k]);
    line[3 * n] = '\0';
    ESP_LOGI(TAG, "  [%8u ms] %-22s %s", static_cast<unsigned>(a.ms), a.reason, line);
  }
  ESP_LOGI(TAG, "=== END ===");
}

void FlexitSL4RComponent::dispatch_frame_() {
  // Raw logging of every validated frame - toggled at runtime.
  if (this->raw_logging_) {
    char line[3 * 40 + 1];
    const size_t n = std::min<size_t>(40, this->frame_.size());
    for (size_t k = 0; k < n; k++)
      sprintf(line + 3 * k, "%02X ", this->frame_[k]);
    line[3 * n] = '\0';
    ESP_LOGD(TAG, "FRAME: %s", line);
  }

  // New frame signature? The first time a (TYPE, LEN, bank) combination shows
  // up is worth capturing - that is how an unknown message type reveals
  // itself.
  {
    const uint8_t bank = this->frame_.size() > FRAME_HEADER_LENGTH ? this->frame_[FRAME_HEADER_LENGTH] : 0;
    const uint32_t sig = (static_cast<uint32_t>(this->frame_[FRAME_CHECKSUM_START]) << 16) |
                         (static_cast<uint32_t>(this->frame_[FRAME_LEN_OFFSET]) << 8) | bank;
    if (std::find(this->seen_signatures_.begin(), this->seen_signatures_.end(), sig) ==
        this->seen_signatures_.end()) {
      this->seen_signatures_.push_back(sig);
      // Stay quiet during the learning period - otherwise the whole startup
      // would be noise.
      if (millis() > ANOMALY_LEARN_MS)
        this->note_anomaly_("new frame type");
    }
  }

  const uint8_t type = this->frame_[FRAME_CHECKSUM_START];
  const uint8_t len = this->frame_[FRAME_LEN_OFFSET];

  if (type == TYPE_STATUS && len == STATUS_DATA_LENGTH) {
    // Fill raw_status_ the way parse_and_publish_status_() expects it.
    // Its checksum window ([TYPE, b6, LEN] + 22 data bytes = 25) is exactly the
    // frame's own, so that validation becomes a cheap double check.
    this->status_sync_193_ = type;
    this->status_sync_gap_ = this->frame_[6];
    for (size_t i = 0; i < STATUS_DATA_LENGTH; i++)
      this->raw_status_[i] = this->frame_[FRAME_HEADER_LENGTH + i];
    this->raw_status_[22] = this->frame_[this->frame_.size() - 2];
    this->raw_status_[23] = this->frame_[this->frame_.size() - 1];
    // Fields we believe to be constant. If one of them changes, that is
    // exactly the event we want full context around - and the alarm field is
    // always reported.
    if (this->have_prev_status_) {
      static const uint8_t CONSTANT_FIELDS[] = {0, 1, 3, 7, 8, 10, 12, 16, 17, 18, 19, 21};
      for (uint8_t idx : CONSTANT_FIELDS) {
        if (this->raw_status_[idx] != this->prev_status_[idx]) {
          this->note_anomaly_("constant field changed");
          break;
        }
      }
      if (this->raw_status_[4] != this->prev_status_[4])
        this->note_anomaly_("ALARM FIELD changed");
      // Bit1 of [2] has never been seen set. If it happens, it is either
      // bypass on a unit we did not think had it, or a reading that needs
      // revising. Either way we want the frame.
      if ((this->raw_status_[2] & HEAT_RECOVERY_BYPASS) != (this->prev_status_[2] & HEAT_RECOVERY_BYPASS))
        this->note_anomaly_("BYPASS BIT changed (never seen before)");
    }
    std::copy_n(this->raw_status_.begin(), STATUS_DATA_LENGTH, this->prev_status_.begin());
    this->have_prev_status_ = true;

    this->parse_and_publish_status_();
    return;
  }

  // Firmware strings: CS50 in bank 0x20 reg 0x00, panel in bank 0x22 reg 0x00.
  if (type == TYPE_STATUS && this->frame_.size() > FRAME_HEADER_LENGTH + 9) {
    const uint8_t node = this->frame_[1];
    const uint8_t bank = this->frame_[FRAME_HEADER_LENGTH];
    const uint8_t reg = this->frame_[FRAME_HEADER_LENGTH + 1];
    if (node == 1 && bank == 0x20 && reg == 0x00) {
      this->publish_firmware_(true);
      return;
    }
    if (bank == 0x22 && reg == 0x00) {
      this->publish_firmware_(false);
      return;
    }
  }

  // The panel's own state frame - the source of the fields we mirror into our
  // own writes.
  if (type == TYPE_STATUS && len == 8 && this->frame_.size() > FRAME_HEADER_LENGTH + 2 &&
      this->frame_[FRAME_HEADER_LENGTH] == 0x20 && this->frame_[FRAME_HEADER_LENGTH + 1] == 0x0F) {
    this->handle_panel_frame_();
    return;
  }

  if (type == TYPE_FLOAT || type == TYPE_PARAM) {
    this->handle_float_frame_();
    return;
  }

  if (type == TYPE_INT)
    this->handle_int_frame_();
}

void FlexitSL4RComponent::handle_int_frame_() {
#ifdef USE_SENSOR
  // 0xC6: payload[0] = bank, payload[1] = register index (counted in WORDS -
  // it steps 0x00/0x0E/0x1C because each frame carries 14 16-bit words). The
  // words are BIG endian (`00 19` = 25), unlike the floats which are little
  // endian.
  if (this->int_register_sensors_.empty())
    return;
  const uint8_t bank = this->frame_[FRAME_HEADER_LENGTH];
  const uint8_t reg_base = this->frame_[FRAME_HEADER_LENGTH + 1];
  const uint8_t *data = this->frame_.data() + FRAME_HEADER_LENGTH + 2;
  const size_t words = (this->frame_[FRAME_LEN_OFFSET] - 2) / 2;

  for (auto &irs : this->int_register_sensors_) {
    if (irs.bank != bank || irs.reg != reg_base || irs.index >= words)
      continue;
    const uint8_t hi = data[irs.index * 2];
    const uint8_t lo = data[irs.index * 2 + 1];
    int32_t value;
    switch (irs.mode) {
      case 1:
        value = hi;
        break;
      case 2:
        value = lo;
        break;
      default:
        value = (static_cast<int32_t>(hi) << 8) | lo;
        break;
    }
    // Publish only on change - the frame repeats every couple of seconds,
    // while parameters and counters change from never to once an hour.
    if (value != irs.last_value) {
      irs.last_value = value;
      irs.sensor->publish_state(value);
    }
  }
#endif
}

void FlexitSL4RComponent::publish_firmware_(bool controller) {
#ifdef USE_TEXT_SENSOR
  auto *sens = controller ? this->controller_firmware_text_sensor_ : this->panel_firmware_text_sensor_;
  if (sens == nullptr)
    return;
  // 8 ASCII bytes right after bank/reg. Trailing spaces and nulls are trimmed.
  std::string v;
  for (size_t k = FRAME_HEADER_LENGTH + 2; k < FRAME_HEADER_LENGTH + 10 && k < this->frame_.size(); k++) {
    const char c = static_cast<char>(this->frame_[k]);
    if (c >= 32 && c < 127)
      v.push_back(c);
  }
  while (!v.empty() && v.back() == ' ')
    v.pop_back();
  // Publish only on change - the frame repeats ~850 times per capture.
  if (!v.empty() && v != sens->get_state())
    sens->publish_state(v);
#endif
}

void FlexitSL4RComponent::handle_panel_frame_() {
  // Mirror the panel's ENTIRE state. That lets a write from us reuse every
  // field we do not understand and change only what we mean to change.
  std::copy_n(this->frame_.begin() + FRAME_HEADER_LENGTH, 8, this->panel_state_.begin());
  this->have_panel_state_ = true;

  // The afterheater state is NO LONGER read from here - the status telegram's
  // [6] bit7 says the same thing and arrives every second. The panel frame is
  // used only to mirror the fields we do not understand into our own writes.
}

void FlexitSL4RComponent::set_afterheat_enabled(bool on) {
  // Set optimistically here because queue_state_frame_() reads the field while
  // building the frame. The next status telegram overwrites it with the unit's
  // actual answer.
  this->afterheat_enabled_ = on;
  ESP_LOGI(TAG, "Writing afterheater %s", on ? "ON" : "OFF");
  this->queue_state_frame_(this->last_raw_fan_level_, BUTTON_NONE, this->last_raw_heat_exchanger_temp_);
}

void FlexitSL4RComponent::handle_float_frame_() {
  // payload[0] = bank, payload[1] = register index. The index counts in
  // REGISTERS (4-byte floats), not bytes - it steps 0, 7, 14, 21 because each
  // frame carries seven floats. See PROTOCOL.md.
  const uint8_t type = this->frame_[FRAME_CHECKSUM_START];
  const uint8_t reg_base = this->frame_[FRAME_HEADER_LENGTH + 1];
  const uint8_t *data = this->frame_.data() + FRAME_HEADER_LENGTH + 2;
  const size_t data_len = this->frame_[FRAME_LEN_OFFSET] - 2;
  const size_t slots = data_len / 4;

  for (size_t slot = 0; slot < slots; slot++) {
    float value;
    memcpy(&value, data + slot * 4, 4);

    // The CS50 reports -55 for a sensor input that is not connected. Showing
    // "-55 degC" in Home Assistant is misleading; NAN gives `unavailable`,
    // which is exactly what it means. This doubles as free hardware detection:
    // entities for options that are not fitted hide themselves.
    if (value == SENSOR_DISCONNECTED)
      value = NAN;

    // Supply air is mirrored regardless - the climate entity needs it as
    // "current temperature", and it does not arrive in the status telegram.
    if (type == TYPE_FLOAT && reg_base == 0 && slot == 1)
      this->last_supply_air_temp_ = value;

#ifdef USE_SENSOR
    if (type == TYPE_FLOAT) {
      if (reg_base == 0 && slot == 1 && this->supply_air_temperature_sensor_ != nullptr)
        this->supply_air_temperature_sensor_->publish_state(value);
      if (reg_base == 7 && slot == 1 && this->heat_exchanger_setpoint_raw_sensor_ != nullptr)
        this->heat_exchanger_setpoint_raw_sensor_->publish_state(value);
    }

    for (const auto &frs : this->float_register_sensors_) {
      if (frs.type == type && frs.reg == reg_base && frs.slot == slot)
        frs.sensor->publish_state(value);
    }
#endif
  }
}

void FlexitSL4RComponent::parse_and_publish_status_() {
  // Sjekksum-vindu: [193, gap, 22] + rawData[0..21] (25 byte totalt).
  std::array<uint8_t, 3 + STATUS_DATA_LENGTH> checksum_input{};
  checksum_input[0] = this->status_sync_193_;
  checksum_input[1] = this->status_sync_gap_;
  checksum_input[2] = 22;
  for (size_t i = 0; i < STATUS_DATA_LENGTH; i++) {
    checksum_input[3 + i] = this->raw_status_[i];
  }
  const auto [sum1, sum2] = checksum_(checksum_input.data(), checksum_input.size());
  const uint8_t checksum_a = this->raw_status_[22];
  const uint8_t checksum_b = this->raw_status_[23];
  if (sum1 != checksum_a || sum2 != checksum_b) {
    ESP_LOGW(TAG, "Status telegram discarded: checksum failed (received %u/%u, computed %u/%u)", checksum_a, checksum_b,
              sum1, sum2);
    return;
  }

  const uint32_t now_ms = millis();
#ifdef USE_SENSOR
  // Published only when a status telegram actually arrives, so it reports the
  // real interval without spamming the recorder.
  if (this->status_interval_sensor_ != nullptr && this->last_valid_telegram_ms_ != 0)
    this->status_interval_sensor_->publish_state((now_ms - this->last_valid_telegram_ms_) / 1000.0f);
#endif
  this->last_valid_telegram_ms_ = now_ms;

  const uint8_t raw_fan_level = this->raw_status_[5];
  const uint8_t raw_afterheat = this->raw_status_[6];
  const uint8_t raw_heat_exchanger_temp = this->raw_status_[9];

  // Fan level is TWO NIBBLES, not one number (see PROTOCOL.md):
  //   high nibble = the level the unit is actually running
  //   low nibble  = the level it returns to when boost ends
  // 0x11/0x22/0x33 = normal operation, 0x31 = boost ("max fan"): runs level 3,
  // falls back to level 1.
  //
  // The old code accepted only 17/34/51 and computed raw/17. That gave two
  // bugs: boost (0x31) was REJECTED, so the entity froze on the previous
  // level, and had it passed, 49/17=2 would have given the wrong level.
  const uint8_t running_level = raw_fan_level >> 4;
  const uint8_t return_level = raw_fan_level & 0x0F;
  if (running_level >= 1 && running_level <= 3 && return_level >= 1 && return_level <= 3) {
    this->last_raw_fan_level_ = raw_fan_level;
#ifdef USE_SELECT
    if (this->fan_level_select_ != nullptr) {
      this->fan_level_select_->publish_state(std::to_string(running_level));
    }
#endif
#ifdef USE_SENSOR
    if (this->fan_level_running_sensor_ != nullptr)
      this->fan_level_running_sensor_->publish_state(running_level);
    if (this->fan_level_return_sensor_ != nullptr)
      this->fan_level_return_sensor_->publish_state(return_level);
#endif
#ifdef USE_BINARY_SENSOR
    // Boost = the unit is running a different level than the one it will fall
    // back to. A precise indicator, free from data we already have.
    if (this->boost_active_binary_sensor_ != nullptr)
      this->boost_active_binary_sensor_->publish_state(running_level != return_level);
#endif
  }

#ifdef USE_SENSOR
  // Fan duty in percent for the two fans (49 / 74 / 100 % at level 1/2/3).
  // Confirmed by following the HIGH nibble of payload[5], not value/17.
  if (this->fan_duty_supply_sensor_ != nullptr)
    this->fan_duty_supply_sensor_->publish_state(this->raw_status_[13]);
  if (this->fan_duty_extract_sensor_ != nullptr)
    this->fan_duty_extract_sensor_->publish_state(this->raw_status_[14]);

  // payload[11] - heat demand. It sat constant at 0 in every earlier capture,
  // and ramped monotonically 0 -> 68 when the setpoint was forced to maximum.
  // Flexit lists J5 (pin 11,12) as "control signal to heat recovery, 0-10 V,
  // 10 V at maximum heat demand", which regulates rotor speed. The scale is
  // assumed to be 0-100.
  if (this->heat_demand_sensor_ != nullptr)
    this->heat_demand_sensor_->publish_state(this->raw_status_[11]);

  // Exploration sensors: let the Home Assistant recorder build history on
  // fields we do not yet understand, so hypotheses can be tested against weeks
  // of data instead of a fresh uart-debug capture.
  for (const auto &entry : this->raw_status_sensors_) {
    if (entry.first < STATUS_DATA_LENGTH)
      entry.second->publish_state(this->raw_status_[entry.first]);
  }
#endif

  // The setpoint MUST be mirrored from the bus, not merely displayed:
  // last_raw_heat_exchanger_temp_ goes into every outgoing state frame. Without
  // this update a fan-level write would send a stale setpoint and thereby
  // overwrite the user's setting.
  if (raw_heat_exchanger_temp > 14 && raw_heat_exchanger_temp < 26) {
    this->last_raw_heat_exchanger_temp_ = raw_heat_exchanger_temp;
#ifdef USE_NUMBER
    if (this->heat_exchanger_setpoint_number_ != nullptr)
      this->heat_exchanger_setpoint_number_->publish_state(raw_heat_exchanger_temp);
#endif
  }

  // payload[6] is a bit field with TWO independent bits: bit0 = boost,
  // bit7 = afterheater enabled. See STATUS_* in the header for how the three
  // earlier readings were disproved.
  //
  // The afterheater is read HERE, not from the panel frame. The status telegram
  // arrives every second; the panel frame only on change. With the panel frame
  // as the sole source the field sat at its default (off) after every restart -
  // and since the value is mirrored into all our writes, the first fan command
  // afterwards switched the afterheater off without anyone asking for it.
  const bool boost_bit = (raw_afterheat & STATUS_BOOST_ACTIVE) != 0;
  this->afterheat_enabled_ = (raw_afterheat & STATUS_AFTERHEAT_ENABLED) != 0;
  this->have_afterheat_state_ = true;
  this->last_raw_afterheat_ = raw_afterheat;

#ifdef USE_BINARY_SENSOR
  if (this->afterheat_active_binary_sensor_ != nullptr)
    this->afterheat_active_binary_sensor_->publish_state(boost_bit);
  if (this->afterheat_enabled_binary_sensor_ != nullptr)
    this->afterheat_enabled_binary_sensor_->publish_state(this->afterheat_enabled_);
  if (this->filter_alarm_binary_sensor_ != nullptr)
    this->filter_alarm_binary_sensor_->publish_state((this->raw_status_[4] & ALARM_FILTER) != 0);
  // Every alarm bit except the filter bit. If this goes on, the red alarm LED
  // has probably lit - read the raw [4] sensor and the anomaly log to find
  // WHICH bit (rotor alarm and overheat thermostat are the candidates).
  if (this->unknown_alarm_binary_sensor_ != nullptr)
    this->unknown_alarm_binary_sensor_->publish_state((this->raw_status_[4] & ~ALARM_FILTER) != 0);
  if (this->heat_recovery_active_binary_sensor_ != nullptr)
    this->heat_recovery_active_binary_sensor_->publish_state((this->raw_status_[2] & HEAT_RECOVERY_RUNNING) != 0);
  if (this->bypass_active_binary_sensor_ != nullptr)
    this->bypass_active_binary_sensor_->publish_state((this->raw_status_[2] & HEAT_RECOVERY_BYPASS) != 0);
#endif

#ifdef USE_CLIMATE
  this->publish_climate_();
#endif
}

#ifdef USE_CLIMATE
void FlexitSL4RComponent::publish_climate_() {
  if (this->ventilation_climate_ == nullptr)
    return;
  // Boost = the unit runs a different level than the one it falls back to. The
  // fan mode should show the RETURN LEVEL, not 3 - that is the user's chosen
  // level, and the one the unit returns to when boost ends.
  const uint8_t running = this->last_raw_fan_level_ >> 4;
  const uint8_t ret = this->last_raw_fan_level_ & 0x0F;
  const bool boost = running != ret;
  // "Heating now" is derived from heat demand ([11]), not from [6] bit0 - that
  // bit turned out to be boost. A demand above zero means the unit is actually
  // calling for heat.
  const bool heating = this->raw_status_[11] > 0;
  this->ventilation_climate_->publish_from_bus(this->last_supply_air_temp_, this->last_raw_heat_exchanger_temp_,
                                               boost ? ret : running, boost, this->afterheat_enabled_, heating);
}
#endif

// The panel's state frame, sent as a poll response. Every field is mirrored
// from the last known state and only the requested one is changed - unsent
// fields would otherwise overwrite reality. See PROTOCOL.md.
void FlexitSL4RComponent::queue_state_frame_(uint8_t fan, uint8_t flag, uint8_t setpoint) {
  // Start from the panel's last known state and override ONLY what we intend
  // to change. Everything else is mirrored - the only safe approach while
  // several fields remain unexplained.
  std::array<uint8_t, 8> data = this->panel_state_;
  data[3] = fan;
  data[4] = static_cast<uint8_t>((this->afterheat_enabled_ ? AFTERHEAT_ENABLED_BIT : 0x00) | flag);
  data[7] = setpoint;

  const std::array<uint8_t, 11> body{0xC1,    this->source_node_, 0x08,    data[0], data[1], data[2],
                                     data[3], data[4],            data[5], data[6], data[7]};
  this->queue_raw_frame_(std::vector<uint8_t>(body.begin(), body.end()));
}

void FlexitSL4RComponent::set_fan_level(uint8_t level) {
  if (level < 1 || level > 3) {
    ESP_LOGW(TAG, "Invalid fan level %u (valid: 1-3)", level);
    return;
  }
  // The fan byte in a COMMAND encodes (previous, new) - the opposite of the
  // status byte, where the high nibble is the running level. Observed from the
  // panel: 0x32 on a 3->2 transition, 0x21 on 2->1, 0x11 at rest on level 1.
  // Sending 0x22 for "level 2" is therefore wrong - the panel never sends that
  // combination.
  const uint8_t prev = this->last_raw_fan_level_ >> 4;
  const uint8_t cmd = static_cast<uint8_t>((prev << 4) | level);
  ESP_LOGI(TAG, "Writing fan level %u (from %u, byte %02X)", static_cast<unsigned>(level),
           static_cast<unsigned>(prev), cmd);
  this->queue_state_frame_(cmd, 0x00, this->last_raw_heat_exchanger_temp_);
}

void FlexitSL4RComponent::queue_raw_frame_(std::vector<uint8_t> frame_without_checksum, uint8_t repeats) {
  this->tx_queue_.push_back({std::move(frame_without_checksum), repeats});
  this->command_queued_ms_ = millis();
}

void FlexitSL4RComponent::send_queued_frame_() {
  if (this->tx_queue_.empty())
    return;
  std::vector<uint8_t> frame = this->tx_queue_.front().bytes;
  const auto [sum1, sum2] = checksum_(frame.data() + FRAME_CHECKSUM_START, frame.size() - FRAME_CHECKSUM_START);
  frame.push_back(sum1);
  frame.push_back(sum2);

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);
  this->write_array(frame.data(), frame.size());
  this->flush();
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);

  ESP_LOGD(TAG, "Sent frame (%u bytes), still queued: %u", static_cast<unsigned>(frame.size()),
           static_cast<unsigned>(this->tx_queue_.size() - 1));

  if (--this->tx_queue_.front().repeats == 0)
    this->tx_queue_.erase(this->tx_queue_.begin());
}

void FlexitSL4RComponent::reset_filter_timer() {
  // The full sequence from the CI 50 manual, in three poll responses. The queue
  // sends one reply per poll, so the frames reach the bus in order with real
  // arbitration between them.
  const uint8_t restore = this->last_raw_heat_exchanger_temp_;
  ESP_LOGW(TAG, "RESETTING THE FILTER TIMER (setpoint %u -> 20 -> %u)", static_cast<unsigned>(restore),
           static_cast<unsigned>(restore));

  // 1) To 20 degrees - the manual requires it, and without that step the alarm
  //    is merely acknowledged while the timer keeps counting.
  this->queue_state_frame_(this->last_raw_fan_level_, BUTTON_NONE, FILTER_RESET_SETPOINT);
  // 2) The reset itself: both temperature buttons, setpoint still at 20.
  this->queue_state_frame_(this->last_raw_fan_level_, PANEL_BUTTON_BIT, FILTER_RESET_SETPOINT);
  // 3) Back to the setpoint the user actually had.
  this->queue_state_frame_(this->last_raw_fan_level_, BUTTON_NONE, restore);
}

void FlexitSL4RComponent::trigger_boost() {
  // Exactly the frame the CI50 sends on a "max fan" press, captured from the
  // bus and checksum-verified:
  //   C3 04 00 C7 51 C1 04 04 20 14 31 23 51 B4
  // The last two bytes are the checksum and are computed at send time, so they
  // are not listed here. The CI50 sends TWO frames on a boost press, not one:
  //   #2303  20 0F 02 <vifte> 01 04 00 <settpunkt>   tilstandsramme, data[4]=01
  //   #2308  20 14 31 23                             selve kommandoen
  // Sending only the last one produced no reaction, even though the frame was
  // byte-identical and demonstrably reached the bus. We therefore mirror the
  // whole sequence, with the current fan level and setpoint in the state frame
  // just as the panel does.
  ESP_LOGI(TAG, "Queueing boost sequence as node %u (sent as a poll response)",
           static_cast<unsigned>(this->source_node_));

  // The state frame is built via queue_state_frame_(), NOT hardcoded: it
  // mirrors the panel's last frame and preserves the afterheater bit. The old
  // hardcoded variant set data[2]=0x02 and data[4]=0x01 blindly and thereby
  // switched the afterheater OFF on every single boost press - the same
  // mirroring bug that had already bitten us twice.
  // b6 repeats the node number - verified on every frame in the capture. If we
  // send as node 5, b6 must be 5 too, or the frame is inconsistent.
  this->queue_state_frame_(this->last_raw_fan_level_, 0x01, this->last_raw_heat_exchanger_temp_);

  const uint8_t n = this->source_node_;

  const std::array<uint8_t, 7> cmd_body{0xC1, n, 0x04, 0x20, 0x14, 0x31, 0x23};
  this->queue_raw_frame_(std::vector<uint8_t>(cmd_body.begin(), cmd_body.end()));
}

void FlexitSL4RComponent::cancel_boost() {
  // Writing the current fan level cancels boost (verified: duty fell
  // 100 -> 49 %). We write the RETURN LEVEL (low nibble), that is, exactly the
  // level the unit is going back to when the period ends - not the high nibble,
  // which during boost is 3.
  const uint8_t return_level = this->last_raw_fan_level_ & 0x0F;
  if (return_level < 1 || return_level > 3) {
    ESP_LOGW(TAG, "Return level not known yet - not cancelling boost");
    return;
  }
  ESP_LOGI(TAG, "Cancelling boost: writing return level %u", static_cast<unsigned>(return_level));
  this->set_fan_level(return_level);
}

void FlexitSL4RComponent::set_heat_exchanger_setpoint(uint8_t celsius) {
  if (celsius < 15 || celsius > 25) {
    ESP_LOGW(TAG, "Invalid setpoint %u (valid: 15-25)", celsius);
    return;
  }
  ESP_LOGI(TAG, "Writing setpoint %u degrees", static_cast<unsigned>(celsius));
  this->queue_state_frame_(this->last_raw_fan_level_, 0x00, celsius);
}

std::pair<uint8_t, uint8_t> FlexitSL4RComponent::checksum_(const uint8_t *data, size_t len) {
  uint16_t sum1 = 0;
  uint16_t sum2 = 0;
  for (size_t i = 0; i < len; i++) {
    sum1 = static_cast<uint16_t>((sum1 + data[i]) & 0xFF);
    sum2 = static_cast<uint16_t>((sum2 + sum1) & 0xFF);
  }
  return {static_cast<uint8_t>(sum1), static_cast<uint8_t>(sum2)};
}

}  // namespace esphome::flexit_sl4r
