#include "flexit_sl4r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace esphome::flexit_sl4r {

static const char *const TAG = "flexit_sl4r";

void FlexitSL4RComponent::dump_boot_capture() {
  ESP_LOGI(TAG, "=== OPPSTARTSFANGST: %u byte ===", static_cast<unsigned>(this->boot_capture_.size()));
  char line[3 * 32 + 1];
  for (size_t i = 0; i < this->boot_capture_.size(); i += 32) {
    const size_t n = std::min<size_t>(32, this->boot_capture_.size() - i);
    for (size_t k = 0; k < n; k++)
      sprintf(line + 3 * k, "%02X ", this->boot_capture_[i + k]);
    line[3 * n] = '\0';
    ESP_LOGI(TAG, "BOOT %04u: %s", static_cast<unsigned>(i), line);
  }
  ESP_LOGI(TAG, "=== SLUTT PÅ OPPSTARTSFANGST ===");
}

void FlexitSL4RComponent::setup() {
  this->boot_capture_.reserve(BOOT_CAPTURE_MAX);
  // Publiser en definert starttilstand. Uten dette blir entiteten stående på
  // `unknown` helt til verdien SKIFTER — og siden loop() kun publiserer ved
  // endring, betyr det i praksis at en frisk node aldri rapporterer «OK».
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
  LOG_SELECT("  ", "Viftetrinn", this->fan_level_select_);
#endif
#ifdef USE_NUMBER
  LOG_NUMBER("  ", "Settpunkt varmeveksler", this->heat_exchanger_setpoint_number_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Forvarme aktiv", this->afterheat_active_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Kommunikasjon OK", this->communication_binary_sensor_);
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

  // Sending skjer KUN når bussen har vært målt stille. Å sende «rett etter en
  // ferdig ramme» kolliderte i praksis, fordi CS50 begynner på neste telegram
  // før det (se research/protocol-notes.md → «Kollisjonen»).
  // NB: når vi svarer på poll, skal køen KUN tømmes av send_poll_response_().
  // Den gamle «send når bussen er stille»-stien stjal ellers rammen og la den
  // ut uoppfordret — og på en polled buss lytter ingen da. Det var årsaken til
  // at viftetrinn ikke virket selv med byte-identisk innhold.
  // NB: guarden gjelder KUN sendeblokka. Den lå tidligere som et `return` her,
  // og siden poll-modus alltid er på, hoppet den samtidig over helsesjekken
  // nedenfor — det var hele årsaken til at «Kommunikasjon OK» aldri ble `on`.
  if (!this->respond_to_polls_ && !this->tx_queue_.empty() && !this->collecting_frame_) {
    const uint32_t now = millis();
    if (now - this->last_rx_byte_ms_ >= BUS_IDLE_BEFORE_TX_MS) {
      this->send_queued_frame_();
    } else if (now - this->command_queued_ms_ > COMMAND_GIVE_UP_MS) {
      ESP_LOGW(TAG, "Fant aldri et stille vindu pa bussen - forkaster %u koede ramme(r)",
               static_cast<unsigned>(this->tx_queue_.size()));
      this->tx_queue_.clear();
    }
  }

  // Er vi fortsatt i CS50s pollerunde? Faller dette bort, feiler all skriving
  // stille — da må aggregatet strømsykles for å re-enumerere oss.
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
        ESP_LOGW(TAG, "Ikke lenger pollet av CS50 - skriving vil feile stille til aggregatet restartes");
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
      ESP_LOGW(TAG, "Ingen gyldige rammer siste %u ms", static_cast<unsigned>(COMMUNICATION_TIMEOUT_MS));
    }
  }
}

void FlexitSL4RComponent::send_poll_response_() {
  // Svaret er KUN kroppen: <TYPE> <node> <LEN> <data...> <CK CK>.
  // C3-headeren tilhører pollen og skal IKKE gjentas — det var nettopp den
  // feilen som gjorde at alle tidligere sendeforsøk ble ignorert.
  std::vector<uint8_t> body;
  if (!this->tx_queue_.empty()) {
    body = this->tx_queue_.front().bytes;
    if (--this->tx_queue_.front().repeats == 0)
      this->tx_queue_.erase(this->tx_queue_.begin());
  } else {
    // «Ingenting å melde» — samme korte svar som CI50 gir mellom hendelser.
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
  ESP_LOGD(TAG, "Svarte på poll til node %u (%u byte)", static_cast<unsigned>(this->source_node_),
           static_cast<unsigned>(body.size()));
}

void FlexitSL4RComponent::handle_incoming_byte_(uint8_t byte) {
  // --- Poll-deteksjon ---
  // Rullende 5-byte vindu. Treffer det pollen for VÅR node, svarer vi straks.
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
    // Forkastes stille i loggen — en 0xC3 inne i en payload treffer denne
    // grenen helt normalt — men TELLES, slik at ekte korrupsjon kan måles.
    // Uten telleren er en ødelagt buss usynlig, og da kan man ikke avgjøre om
    // vår egen sending kolliderer med CS50.
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
  // Logges også med en gang, så den fanges av en vanlig loggstrøm.
  char line[3 * 40 + 1];
  const size_t n = std::min<size_t>(40, this->frame_.size());
  for (size_t k = 0; k < n; k++)
    sprintf(line + 3 * k, "%02X ", this->frame_[k]);
  line[3 * n] = '\0';
  ESP_LOGW(TAG, "ANOMALI (%s): %s", reason, line);
}

void FlexitSL4RComponent::dump_anomalies() {
  ESP_LOGI(TAG, "=== ANOMALIER: %u lagret, %u totalt siden oppstart ===",
           static_cast<unsigned>(this->anomalies_.size()), static_cast<unsigned>(this->anomaly_count_));
  char line[3 * 40 + 1];
  for (const auto &a : this->anomalies_) {
    const size_t n = std::min<size_t>(40, a.frame.size());
    for (size_t k = 0; k < n; k++)
      sprintf(line + 3 * k, "%02X ", a.frame[k]);
    line[3 * n] = '\0';
    ESP_LOGI(TAG, "  [%8u ms] %-22s %s", static_cast<unsigned>(a.ms), a.reason, line);
  }
  ESP_LOGI(TAG, "=== SLUTT ===");
}

void FlexitSL4RComponent::dispatch_frame_() {
  // Rå logging av hver validerte ramme — skrus av/på i drift.
  if (this->raw_logging_) {
    char line[3 * 40 + 1];
    const size_t n = std::min<size_t>(40, this->frame_.size());
    for (size_t k = 0; k < n; k++)
      sprintf(line + 3 * k, "%02X ", this->frame_[k]);
    line[3 * n] = '\0';
    ESP_LOGD(TAG, "RAMME: %s", line);
  }

  // Ny rammesignatur? Første gang en (TYPE, LEN, bank)-kombinasjon dukker opp
  // er den verdt å fange — det er slik en ukjent meldingstype røper seg.
  {
    const uint8_t bank = this->frame_.size() > FRAME_HEADER_LENGTH ? this->frame_[FRAME_HEADER_LENGTH] : 0;
    const uint32_t sig = (static_cast<uint32_t>(this->frame_[FRAME_CHECKSUM_START]) << 16) |
                         (static_cast<uint32_t>(this->frame_[FRAME_LEN_OFFSET]) << 8) | bank;
    if (std::find(this->seen_signatures_.begin(), this->seen_signatures_.end(), sig) ==
        this->seen_signatures_.end()) {
      this->seen_signatures_.push_back(sig);
      // Ikke meld fra i læringsperioden — da ville hele oppstarten blitt støy.
      if (millis() > ANOMALY_LEARN_MS)
        this->note_anomaly_("ny rammetype");
    }
  }

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
    // Felt vi tror er konstante. Endrer ett av dem seg, er det nettopp den
    // hendelsen vi vil ha full kontekst rundt — og alarmfeltet varsles alltid.
    if (this->have_prev_status_) {
      static const uint8_t CONSTANT_FIELDS[] = {0, 1, 3, 7, 8, 10, 12, 16, 17, 18, 19, 21};
      for (uint8_t idx : CONSTANT_FIELDS) {
        if (this->raw_status_[idx] != this->prev_status_[idx]) {
          this->note_anomaly_("konstant felt endret");
          break;
        }
      }
      if (this->raw_status_[4] != this->prev_status_[4])
        this->note_anomaly_("ALARMFELT endret");
    }
    std::copy_n(this->raw_status_.begin(), STATUS_DATA_LENGTH, this->prev_status_.begin());
    this->have_prev_status_ = true;

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

    // CS50 rapporterer -55 for en følerinngang som ikke er tilkoblet. Å vise
    // «-55 °C» i HA er misvisende; NAN gir `unavailable`, som er nettopp det
    // det betyr. Dette er samtidig en gratis maskinvaredeteksjon: entiteter for
    // tilvalg som ikke er montert skjuler seg selv.
    if (value == SENSOR_DISCONNECTED)
      value = NAN;

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

  const uint32_t now_ms = millis();
#ifdef USE_SENSOR
  // Publiseres kun når et statustelegram faktisk kommer, så den forteller det
  // reelle intervallet uten å spamme recorderen.
  if (this->status_interval_sensor_ != nullptr && this->last_valid_telegram_ms_ != 0)
    this->status_interval_sensor_->publish_state((now_ms - this->last_valid_telegram_ms_) / 1000.0f);
#endif
  this->last_valid_telegram_ms_ = now_ms;

  const uint8_t raw_fan_level = this->raw_status_[5];
  const uint8_t raw_afterheat = this->raw_status_[6];
  const uint8_t raw_heat_exchanger_temp = this->raw_status_[9];

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

  // payload[6] er et bitfelt, ikke en av/på-verdi. Se AFTERHEAT_* i headeren.
  const bool afterheat_heating = (raw_afterheat & AFTERHEAT_HEATING) != 0;
  const bool afterheat_enabled = (raw_afterheat & AFTERHEAT_DISABLED) == 0;
  this->last_raw_afterheat_ = raw_afterheat;

#ifdef USE_BINARY_SENSOR
  if (this->afterheat_active_binary_sensor_ != nullptr)
    this->afterheat_active_binary_sensor_->publish_state(afterheat_heating);
  if (this->afterheat_enabled_binary_sensor_ != nullptr)
    this->afterheat_enabled_binary_sensor_->publish_state(afterheat_enabled);
  if (this->filter_alarm_binary_sensor_ != nullptr)
    this->filter_alarm_binary_sensor_->publish_state((this->raw_status_[4] & ALARM_FILTER) != 0);
#endif
}

// Panelets tilstandsramme, som poll-svar. Alle felt speiles fra sist kjente
// tilstand, og kun det ønskede endres — usendte felt ville ellers overskrevet
// virkeligheten. Se research/protocol-notes.md → «Polled buss».
void FlexitSL4RComponent::queue_state_frame_(uint8_t fan, uint8_t flag, uint8_t setpoint) {
  const std::array<uint8_t, 11> body{0xC1, this->source_node_, 0x08, 0x20,     0x0F, 0x02,
                                     fan,  flag,               0x04, 0x00,     setpoint};
  this->queue_raw_frame_(std::vector<uint8_t>(body.begin(), body.end()));
}

void FlexitSL4RComponent::set_fan_level(uint8_t level) {
  if (level < 1 || level > 3) {
    ESP_LOGW(TAG, "Ugyldig viftetrinn %u (gyldig: 1-3)", level);
    return;
  }
  // Viftebyten i KOMMANDOEN koder (forrige, nytt) — motsatt av statusbyten,
  // der høy nibbel er trinnet som kjører. Observert hos panelet:
  //   0x32 ved overgangen 3->2, 0x21 ved 2->1, 0x11 i ro på trinn 1.
  // Å sende 0x22 for «trinn 2» er derfor feil — den kombinasjonen sender
  // panelet aldri.
  const uint8_t prev = this->last_raw_fan_level_ >> 4;
  const uint8_t cmd = static_cast<uint8_t>((prev << 4) | level);
  ESP_LOGI(TAG, "Skriver viftetrinn %u (fra %u, byte %02X)", static_cast<unsigned>(level),
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

  ESP_LOGD(TAG, "Sendte ramme (%u byte), igjen i kø: %u", static_cast<unsigned>(frame.size()),
           static_cast<unsigned>(this->tx_queue_.size() - 1));

  if (--this->tx_queue_.front().repeats == 0)
    this->tx_queue_.erase(this->tx_queue_.begin());
}

void FlexitSL4RComponent::trigger_boost() {
  // Nøyaktig den rammen CI50 sender ved trykk på «Max vifte», fanget fra
  // bussen 2026-08-13 og sjekksum-verifisert:
  //   C3 04 00 C7 51 C1 04 04 20 14 31 23 51 B4
  // De to siste byte er sjekksummen og beregnes ved sending, så de er ikke
  // med her. Rammen er en engangs-kommando uten felt som må speiles fra
  // gjeldende tilstand — derfor går den utenom command_template.
  // CI50 sender TO rammer ved et forseringstrykk, ikke én (målt 2026-08-14):
  //   #2303  20 0F 02 <vifte> 01 04 00 <settpunkt>   tilstandsramme, data[4]=01
  //   #2308  20 14 31 23                             selve kommandoen
  // Å sende kun den siste ga ingen reaksjon, selv om rammen var byte-identisk
  // og beviselig nådde bussen. Vi speiler derfor hele sekvensen, med gjeldende
  // viftetrinn og settpunkt i tilstandsrammen slik panelet gjør.
  ESP_LOGI(TAG, "Køer forseringssekvens som node %u (sendes som poll-svar)",
           static_cast<unsigned>(this->source_node_));

  // b6 gjentar nodenummeret — verifisert på alle rammer i opptaket. Sender vi
  // som node 5 må også b6 være 5, ellers er rammen inkonsistent.
  const uint8_t n = this->source_node_;
  const std::array<uint8_t, 11> state_body{0xC1, n, 0x08, 0x20, 0x0F, 0x02, this->last_raw_fan_level_,
                                           0x01, 0x04, 0x00, this->last_raw_heat_exchanger_temp_};
  this->queue_raw_frame_(std::vector<uint8_t>(state_body.begin(), state_body.end()));

  const std::array<uint8_t, 7> cmd_body{0xC1, n, 0x04, 0x20, 0x14, 0x31, 0x23};
  this->queue_raw_frame_(std::vector<uint8_t>(cmd_body.begin(), cmd_body.end()));
}

void FlexitSL4RComponent::set_heat_exchanger_setpoint(uint8_t celsius) {
  if (celsius < 15 || celsius > 25) {
    ESP_LOGW(TAG, "Ugyldig settpunkt %u (gyldig: 15-25)", celsius);
    return;
  }
  ESP_LOGI(TAG, "Skriver settpunkt %u grader", static_cast<unsigned>(celsius));
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
