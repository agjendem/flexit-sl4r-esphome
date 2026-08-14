#include "flexit_sl4r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cstring>
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
    this->last_rx_byte_ms_ = millis();
    this->handle_incoming_byte_(static_cast<uint8_t>(data));
  }

  // Sending skjer KUN når bussen har vært målt stille. Å sende «rett etter en
  // ferdig ramme» kolliderte i praksis, fordi CS50 begynner på neste telegram
  // før det (se research/protocol-notes.md → «Kollisjonen»).
  if (this->command_pending_ && !this->collecting_frame_) {
    const uint32_t now = millis();
    if (now - this->last_rx_byte_ms_ >= BUS_IDLE_BEFORE_TX_MS) {
      this->build_and_send_command_();
    } else if (now - this->command_queued_ms_ > COMMAND_GIVE_UP_MS) {
      ESP_LOGW(TAG, "Fant aldri et stille vindu på bussen - forkaster kommandoen");
      this->command_pending_ = false;
      this->pending_raw_frame_.clear();
    }
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
  // --- Generell rammeoppsamling ---
  // MERK: den gamle egne «kommandovindu»-tilstandsmaskinen er fjernet. Den
  // spiste byte utenom rammeparseren for å finne et hull å sende i, og hadde
  // to feil som til sammen satte hele mottaket ut av spill (se
  // research/protocol-notes.md → «Arbitreringsfeilen»). Rammeparseren vet
  // allerede nøyaktig når et telegram slutter — det ER hullet — så
  // arbitreringen trenger ingen egen bytelesing.
  if (!this->collecting_frame_) {
    if (byte != FRAME_START)
      return;
    this->collecting_frame_ = true;
    this->frame_expected_ = 0;
    this->frame_.clear();
  }

  this->frame_.push_back(byte);

  // Lengdebyten kommer på offset 7 og bestemmer hvor mye mer vi skal samle.
  if (this->frame_expected_ == 0 && this->frame_.size() == FRAME_HEADER_LENGTH) {
    const uint8_t len = this->frame_[FRAME_LEN_OFFSET];
    if (len > FRAME_MAX_PAYLOAD) {
      // Urimelig lengde: dette var en 0xC3 inne i en payload, ikke en ramme.
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
    // Forkastes stille: en 0xC3 inne i en payload treffer denne grenen ofte, og
    // det er normalt — ikke en feil verdt å logge på hver forekomst.
    return;
  }

  this->dispatch_frame_();
}

void FlexitSL4RComponent::dispatch_frame_() {
  const uint8_t type = this->frame_[FRAME_CHECKSUM_START];
  const uint8_t len = this->frame_[FRAME_LEN_OFFSET];

  if (type == TYPE_STATUS && len == STATUS_DATA_LENGTH) {
    // Fyll raw_status_ slik parse_and_publish_status_() forventer den.
    // Sjekksumvinduet der ([TYPE, b6, LEN] + 22 databyte = 25) er nøyaktig det
    // samme som rammens eget, så den valideringen blir en billig dobbeltsjekk.
    this->status_sync_193_ = type;
    this->status_sync_gap_ = this->frame_[6];
    for (size_t i = 0; i < STATUS_DATA_LENGTH; i++)
      this->raw_status_[i] = this->frame_[FRAME_HEADER_LENGTH + i];
    this->raw_status_[22] = this->frame_[this->frame_.size() - 2];
    this->raw_status_[23] = this->frame_[this->frame_.size() - 1];
    this->parse_and_publish_status_();
    return;
  }

  if (type == TYPE_FLOAT || type == TYPE_PARAM)
    this->handle_float_frame_();
}

void FlexitSL4RComponent::handle_float_frame_() {
#ifdef USE_SENSOR
  // payload[0] = bank, payload[1] = registerindeks. Indeksen teller i REGISTRE
  // (4-byte floats), ikke i byte — den stepper 0, 7, 14, 21 fordi hver ramme
  // bærer sju floats. Se research/protocol-notes.md → «Flyttall-registre».
  const uint8_t type = this->frame_[FRAME_CHECKSUM_START];
  const uint8_t reg_base = this->frame_[FRAME_HEADER_LENGTH + 1];
  const uint8_t *data = this->frame_.data() + FRAME_HEADER_LENGTH + 2;
  const size_t data_len = this->frame_[FRAME_LEN_OFFSET] - 2;
  const size_t slots = data_len / 4;

  for (size_t slot = 0; slot < slots; slot++) {
    float value;
    memcpy(&value, data + slot * 4, 4);

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
  }
#endif
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
#ifdef USE_SENSOR
    if (this->fan_level_running_sensor_ != nullptr)
      this->fan_level_running_sensor_->publish_state(running_level);
    if (this->fan_level_return_sensor_ != nullptr)
      this->fan_level_return_sensor_->publish_state(return_level);
#endif
#ifdef USE_BINARY_SENSOR
    // Forsering = aggregatet kjører på et annet trinn enn det skal falle
    // tilbake til. Presis indikator, gratis fra dataene vi allerede har.
    if (this->boost_active_binary_sensor_ != nullptr)
      this->boost_active_binary_sensor_->publish_state(running_level != return_level);
#endif
  }

#ifdef USE_SENSOR
  // Viftepådrag i prosent for de to viftene (49 / 74 / 100 % ved trinn 1/2/3).
  // Bekreftet ved at de følger HØY nibbel av payload[5], ikke verdi/17.
  if (this->fan_duty_supply_sensor_ != nullptr)
    this->fan_duty_supply_sensor_->publish_state(this->raw_status_[13]);
  if (this->fan_duty_extract_sensor_ != nullptr)
    this->fan_duty_extract_sensor_->publish_state(this->raw_status_[14]);

  // Utforsknings-sensorer: la HAs recorder bygge historikk på felt vi ennå
  // ikke forstår, så hypoteser kan prøves mot uker med data i stedet for et
  // nytt uart-debug-opptak.
  for (const auto &entry : this->raw_status_sensors_) {
    if (entry.first < STATUS_DATA_LENGTH)
      entry.second->publish_state(this->raw_status_[entry.first]);
  }
#endif

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
  this->command_queued_ms_ = millis();
  this->command_repeats_left_ = COMMAND_REPEATS;
}

void FlexitSL4RComponent::set_fan_level(uint8_t level) {
  if (level < 1 || level > 3) {
    ESP_LOGW(TAG, "Ugyldig viftetrinn %u (gyldig: 1-3)", level);
    return;
  }
  this->queue_command_(11, static_cast<uint8_t>(level * 17));
}

void FlexitSL4RComponent::set_preheat(bool on) { this->queue_command_(12, on ? 128 : 0); }

void FlexitSL4RComponent::queue_raw_frame_(std::vector<uint8_t> frame_without_checksum) {
  this->pending_raw_frame_ = std::move(frame_without_checksum);
  this->command_pending_ = true;
  this->command_queued_ms_ = millis();
  this->command_repeats_left_ = COMMAND_REPEATS;
}

void FlexitSL4RComponent::trigger_boost() {
  // Nøyaktig den rammen CI50 sender ved trykk på «Max vifte», fanget fra
  // bussen 2026-08-13 og sjekksum-verifisert:
  //   C3 04 00 C7 51 C1 04 04 20 14 31 23 51 B4
  // De to siste byte er sjekksummen og beregnes ved sending, så de er ikke
  // med her. Rammen er en engangs-kommando uten felt som må speiles fra
  // gjeldende tilstand — derfor går den utenom command_template.
  ESP_LOGI(TAG, "Køer forseringskommando (Max vifte)");
  this->queue_raw_frame_({0xC3, 0x04, 0x00, 0xC7, 0x51, 0xC1, 0x04, 0x04, 0x20, 0x14, 0x31, 0x23});
}

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

  // Hver kommando sendes COMMAND_REPEATS ganger, hver gang i sitt eget stille
  // vindu — jf. Vongravens original, se COMMAND_REPEATS i headeren.
  const bool last_repeat = this->command_repeats_left_ <= 1;
  if (this->command_repeats_left_ > 0)
    this->command_repeats_left_--;
  if (last_repeat)
    this->command_pending_ = false;

  // Engangs-kommando (f.eks. forsering): send den ferdige rammen som den er,
  // kun med sjekksum påført. Går utenom command_template-modellen, som antar
  // en fast 18-byte tilstandsskriving med felt som må speiles.
  if (!this->pending_raw_frame_.empty()) {
    std::vector<uint8_t> frame = this->pending_raw_frame_;  // kopi: rammen skal sendes flere ganger
    const auto [rsum1, rsum2] = checksum_(frame.data() + 5, frame.size() - 5);
    frame.push_back(rsum1);
    frame.push_back(rsum2);

    if (this->flow_control_pin_ != nullptr)
      this->flow_control_pin_->digital_write(true);
    this->write_array(frame.data(), frame.size());
    this->flush();
    if (this->flow_control_pin_ != nullptr)
      this->flow_control_pin_->digital_write(false);

    ESP_LOGD(TAG, "Sendte engangsramme (%u byte), gjentakelser igjen: %u", static_cast<unsigned>(frame.size()),
             static_cast<unsigned>(this->command_repeats_left_));
    if (last_repeat)
      this->pending_raw_frame_.clear();
    return;
  }

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
