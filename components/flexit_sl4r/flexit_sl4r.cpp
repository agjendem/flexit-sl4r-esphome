#include "flexit_sl4r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <string>

namespace esphome::flexit_sl4r {

static const char *const TAG = "flexit_sl4r";

void FlexitSL4RComponent::setup() {
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
  LOG_SELECT("  ", "Viftetrinn", this->fan_level_select_);
#endif
#ifdef USE_SWITCH
  LOG_SWITCH("  ", "Forvarme", this->preheat_switch_);
#endif
#ifdef USE_NUMBER
  LOG_NUMBER("  ", "Settpunkt varmeveksler", this->heat_exchanger_setpoint_number_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Forvarme aktiv", this->preheat_active_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Kommunikasjon OK", this->communication_binary_sensor_);
#endif
}

void FlexitSL4RComponent::loop() {
  while (this->available() != 0) {
    const int data = this->read();
    if (data < 0)
      break;
    this->handle_incoming_byte_(static_cast<uint8_t>(data));
  }

  const bool now_ok =
      this->last_valid_telegram_ms_ != 0 && (millis() - this->last_valid_telegram_ms_) < COMMUNICATION_TIMEOUT_MS;
  if (now_ok != this->communication_ok_) {
    this->communication_ok_ = now_ok;
#ifdef USE_BINARY_SENSOR
    if (this->communication_binary_sensor_ != nullptr) {
      this->communication_binary_sensor_->publish_state(now_ok);
    }
#endif
    if (!now_ok) {
      ESP_LOGW(TAG, "Ingen gyldige statustelegram siste %u ms", static_cast<unsigned>(COMMUNICATION_TIMEOUT_MS));
    }
  }
}

void FlexitSL4RComponent::handle_incoming_byte_(uint8_t byte) {
  // Et pågående kommandovindu- eller status-capture-forløp har alltid forrang;
  // en byte kan aldri tolkes to ganger samtidig av begge tilstandsmaskinene.
  if (this->capturing_status_) {
    this->handle_status_payload_byte_(byte);
    return;
  }
  if (this->command_pending_ && this->cmd_slot_state_ != CmdSlotState::IDLE) {
    this->handle_command_slot_byte_(byte);
    return;
  }

  // Skyv byten inn i den rullende synk-historikken (index 8 = nyeste).
  for (size_t i = 0; i + 1 < this->sync_history_.size(); i++) {
    this->sync_history_[i] = this->sync_history_[i + 1];
  }
  this->sync_history_.back() = byte;

  // Statustelegram-synk: gjeldende byte==22 (lengde), 2 tilbake==193, 7 tilbake==195.
  //
  // MÅLT PÅ EGET ANLEGG 2026-08-13: rammene er C3 b1 b2 b3 b4 TYPE b6 LEN
  // [LEN byte] CK1 CK2, altså ligger 195 (0xC3) SJU byte foran lengdebyten,
  // ikke åtte. Vongravens notat sa i-8, og med den regelen ga 23 708 avlyttede
  // byte NULL treff; med i-7 ga de 41. Se research/protocol-notes.md
  // → «Rammestruktur (målt)».
  if (byte == 22 && this->sync_history_[6] == 193 && this->sync_history_[1] == 195) {
    this->on_status_sync_matched_(this->sync_history_[6], this->sync_history_[7]);
    return;
  }

  // CI50 sender kommando: forrige byte==195, denne==1. Kun relevant å lete etter
  // når vi faktisk har noe å sende (unngår å bruke sykluser på falske positiver ellers).
  if (this->command_pending_ && this->sync_history_[7] == 195 && byte == 1) {
    this->cmd_slot_state_ = CmdSlotState::SKIPPING_HEADER;
    this->cmd_slot_skip_remaining_ = 6;
  }
}

void FlexitSL4RComponent::on_status_sync_matched_(uint8_t sync_193, uint8_t sync_gap) {
  this->status_sync_193_ = sync_193;
  this->status_sync_gap_ = sync_gap;
  this->capturing_status_ = true;
  this->status_bytes_received_ = 0;
}

void FlexitSL4RComponent::handle_status_payload_byte_(uint8_t byte) {
  this->raw_status_[this->status_bytes_received_] = byte;
  this->status_bytes_received_++;
  if (this->status_bytes_received_ >= this->raw_status_.size()) {
    this->capturing_status_ = false;
    this->parse_and_publish_status_();
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
    ESP_LOGW(TAG, "Statustelegram forkastet: sjekksum feilet (mottatt %u/%u, beregnet %u/%u)", checksum_a, checksum_b,
              sum1, sum2);
    return;
  }

  this->last_valid_telegram_ms_ = millis();

  const uint8_t raw_fan_level = this->raw_status_[5];
  const uint8_t raw_preheat = this->raw_status_[6];
  const uint8_t raw_heat_exchanger_temp = this->raw_status_[9];
  const uint8_t raw_preheat_threshold_1 = this->raw_status_[10];
  const uint8_t raw_preheat_threshold_2 = this->raw_status_[11];

  // Viftetrinn er TO NIBBLER, ikke ett tall (målt 2026-08-13, se
  // research/protocol-notes.md → «Viftetrinn er to nibbler»):
  //   høy nibbel = trinnet aggregatet faktisk kjører på
  //   lav nibbel = trinnet det returnerer til når forseringen er over
  // 0x11/0x22/0x33 = vanlig drift, 0x31 = forsering («Max vifte»): kjører
  // trinn 3, faller tilbake til trinn 1.
  //
  // Den gamle koden godtok kun 17/34/51 og regnet raw/17. Det ga to feil:
  // forsering (0x31) ble AVVIST, så entiteten frøs på forrige trinn, og
  // hadde den sluppet gjennom ville 49/17=2 gitt feil trinn.
  const uint8_t running_level = raw_fan_level >> 4;
  const uint8_t return_level = raw_fan_level & 0x0F;
  if (running_level >= 1 && running_level <= 3 && return_level >= 1 && return_level <= 3) {
    this->last_raw_fan_level_ = raw_fan_level;
#ifdef USE_SELECT
    if (this->fan_level_select_ != nullptr) {
      this->fan_level_select_->publish_state(std::to_string(running_level));
    }
#endif
  }

  bool preheat_on = this->last_raw_preheat_ == 128;
  if (raw_preheat == 128 || raw_preheat == 0) {
    this->last_raw_preheat_ = raw_preheat;
    preheat_on = raw_preheat == 128;
#ifdef USE_SWITCH
    if (this->preheat_switch_ != nullptr) {
      this->preheat_switch_->publish_state(preheat_on);
    }
#endif
  }

  if (raw_heat_exchanger_temp > 14 && raw_heat_exchanger_temp < 26) {
    this->last_raw_heat_exchanger_temp_ = raw_heat_exchanger_temp;
#ifdef USE_NUMBER
    if (this->heat_exchanger_setpoint_number_ != nullptr) {
      this->heat_exchanger_setpoint_number_->publish_state(raw_heat_exchanger_temp);
    }
#endif
  }

  // Forvarme-aktiv er en tilstandslås (ikke en ren funksjon av gjeldende telegram):
  // aktiveres når threshold1 > 10, forblir aktiv til threshold2 < 100. Se protocol-notes.md.
  if (preheat_on) {
    if (!this->preheat_active_state_ && raw_preheat_threshold_1 > 10) {
      this->preheat_active_state_ = true;
    } else if (this->preheat_active_state_ && raw_preheat_threshold_2 < 100) {
      this->preheat_active_state_ = false;
    }
  } else {
    this->preheat_active_state_ = false;
  }
#ifdef USE_BINARY_SENSOR
  if (this->preheat_active_binary_sensor_ != nullptr) {
    this->preheat_active_binary_sensor_->publish_state(this->preheat_active_state_);
  }
#endif
}

void FlexitSL4RComponent::handle_command_slot_byte_(uint8_t byte) {
  switch (this->cmd_slot_state_) {
    case CmdSlotState::SKIPPING_HEADER:
      if (--this->cmd_slot_skip_remaining_ == 0) {
        this->cmd_slot_state_ = CmdSlotState::READING_LENGTH;
      }
      break;
    case CmdSlotState::READING_LENGTH: {
      const uint8_t length = static_cast<uint8_t>(byte + 2);
      if (length < 3 || length > 32) {
        // Urimelig lengde: falsk positiv på "195, 1"-mønsteret. Gi opp og gå
        // tilbake til normal synk-skanning i stedet for å hoppe over feil antall byte.
        this->cmd_slot_state_ = CmdSlotState::IDLE;
        break;
      }
      this->cmd_slot_skip_remaining_ = length;
      this->cmd_slot_state_ = CmdSlotState::SKIPPING_PAYLOAD;
      break;
    }
    case CmdSlotState::SKIPPING_PAYLOAD:
      if (--this->cmd_slot_skip_remaining_ == 0) {
        this->cmd_slot_state_ = CmdSlotState::IDLE;
        // Bussen er nå ledig. Kort guard-forsinkelse før vi injiserer, jf. original-
        // implementasjonens delay(10) — planlagt ikke-blokkerende via set_timeout.
        this->set_timeout("flexit_sl4r_cmd", COMMAND_INJECT_DELAY_MS, [this]() { this->build_and_send_command_(); });
      }
      break;
    case CmdSlotState::IDLE:
    default:
      break;
  }
}

void FlexitSL4RComponent::queue_command_(uint8_t field_offset, uint8_t value) {
  // Uten konfigurert command_template har vi ingenting å bygge telegrammet av.
  // Å sende likevel ville lagt 18 udefinerte byte med GYLDIG sjekksum ut på
  // bussen — se research/protocol-notes.md punkt 2. Er malen ikke satt, er
  // Fase 2 ikke aktivert og skrive-entitetene er i praksis read-only.
  if (this->command_template_.size() < COMMAND_LENGTH) {
    ESP_LOGE(TAG,
             "command_template er ikke konfigurert - kommandoen forkastes (Fase 2 ikke aktivert). "
             "Entiteten faller tilbake til faktisk tilstand ved neste statustelegram.");
    return;
  }
  this->pending_field_offset_ = field_offset;
  this->pending_field_value_ = value;
  this->command_pending_ = true;
}

void FlexitSL4RComponent::set_fan_level(uint8_t level) {
  if (level < 1 || level > 3) {
    ESP_LOGW(TAG, "Ugyldig viftetrinn %u (gyldig: 1-3)", level);
    return;
  }
  this->queue_command_(11, static_cast<uint8_t>(level * 17));
}

void FlexitSL4RComponent::set_preheat(bool on) { this->queue_command_(12, on ? 128 : 0); }

void FlexitSL4RComponent::set_heat_exchanger_setpoint(uint8_t celsius) {
  if (celsius < 15 || celsius > 25) {
    ESP_LOGW(TAG, "Ugyldig settpunkt %u (gyldig: 15-25)", celsius);
    return;
  }
  this->queue_command_(15, celsius);
}

void FlexitSL4RComponent::build_and_send_command_() {
  if (!this->command_pending_)
    return;
  this->command_pending_ = false;

  // Dobbel sikring: queue_command_ slipper ikke gjennom uten mal, men denne
  // copy_n leser 18 byte og MÅ aldri kjøre mot en tom vector.
  if (this->command_template_.size() < COMMAND_LENGTH) {
    ESP_LOGE(TAG, "command_template mangler ved sending - avbryter, ingenting skrevet til bussen");
    return;
  }

  std::array<uint8_t, COMMAND_LENGTH> command{};
  std::copy_n(this->command_template_.begin(), COMMAND_LENGTH, command.begin());

  // Speil inn nåværende kjente tilstand (se protocol-notes.md: usendte felt
  // overskrives ellers utilsiktet av CS50 når kommandoen mottas).
  command[11] = this->last_raw_fan_level_;
  command[12] = this->last_raw_preheat_;
  command[15] = this->last_raw_heat_exchanger_temp_;

  // Overstyr med det faktisk ønskede feltet.
  command[this->pending_field_offset_] = this->pending_field_value_;

  const auto [sum1, sum2] = checksum_(command.data() + 5, 11);
  command[16] = sum1;
  command[17] = sum2;

  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(true);
  }
  this->write_array(command);
  this->flush();
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(false);
  }

  ESP_LOGD(TAG, "Sendte Flexit-kommando (felt=%u, verdi=%u)", this->pending_field_offset_,
           this->pending_field_value_);
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
