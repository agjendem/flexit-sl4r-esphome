#include "flexit_climate.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome::flexit_sl4r {

// Viftetrinnene som custom fan modes. Må være statiske const char* — ESPHome
// lagrer peker, ikke kopi, og sammenligner identitet i tillegg til innhold.
static const char *const FAN_MODE_1 = "1";
static const char *const FAN_MODE_2 = "2";
static const char *const FAN_MODE_3 = "3";

climate::ClimateTraits FlexitClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  // Ingen OFF-modus: aggregatet kan ikke stoppes over bussen. HEAT/FAN_ONLY
  // skiller på om ettervarmen er aktivert.
  traits.set_supported_modes({climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY});
  // BOOST = «Max vifte» (dusj/matlaging). Aggregatet faller selv tilbake til
  // returtrinnet når perioden er over.
  traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_BOOST});
  traits.set_visual_min_temperature(15);
  traits.set_visual_max_temperature(25);
  traits.set_visual_target_temperature_step(1);
  traits.set_visual_current_temperature_step(0.1f);
  return traits;
}

void FlexitClimate::setup_state() {
  // Viftemodusene ligger på entiteten (ikke traits) fra ESPHome 2026.5.
  this->set_supported_custom_fan_modes({FAN_MODE_1, FAN_MODE_2, FAN_MODE_3});
}

void FlexitClimate::control(const climate::ClimateCall &call) {
  // Rekkefølgen er bevisst: forsering behandles sist, slik at en samtidig
  // trinn-endring i samme kall ikke avbryter forseringen vi nettopp ba om.
  if (call.get_target_temperature().has_value())
    this->parent_->set_heat_exchanger_setpoint(static_cast<uint8_t>(lroundf(*call.get_target_temperature())));

  if (call.get_mode().has_value())
    this->parent_->set_afterheat_enabled(*call.get_mode() == climate::CLIMATE_MODE_HEAT);

  const auto fm = call.get_custom_fan_mode();
  if (!fm.empty() && fm.size() == 1 && fm.c_str()[0] >= '1' && fm.c_str()[0] <= '3')
    this->parent_->set_fan_level(static_cast<uint8_t>(fm.c_str()[0] - '0'));

  if (call.get_preset().has_value()) {
    if (*call.get_preset() == climate::CLIMATE_PRESET_BOOST) {
      this->parent_->trigger_boost();
    } else {
      this->parent_->cancel_boost();
    }
  }
}

void FlexitClimate::publish_from_bus(float current_temp, uint8_t setpoint, uint8_t fan_level, bool boost,
                                     bool afterheat_enabled, bool heating) {
  bool changed = false;

  if (!std::isnan(current_temp) && current_temp != this->current_temperature) {
    this->current_temperature = current_temp;
    changed = true;
  }

  if (static_cast<float>(setpoint) != this->target_temperature) {
    this->target_temperature = setpoint;
    changed = true;
  }

  const auto mode = afterheat_enabled ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_FAN_ONLY;
  if (mode != this->mode) {
    this->mode = mode;
    changed = true;
  }

  // HEATING når varmepådraget er over null, altså når aggregatet faktisk
  // kaller på varme (rotor og eventuelt ettervarme).
  const auto action = heating ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_FAN;
  if (action != this->action) {
    this->action = action;
    changed = true;
  }

  if (fan_level >= 1 && fan_level <= 3) {
    const char *want = fan_level == 1 ? FAN_MODE_1 : (fan_level == 2 ? FAN_MODE_2 : FAN_MODE_3);
    const auto current = this->get_custom_fan_mode();
    if (current.empty() || strcmp(current.c_str(), want) != 0) {
      if (this->set_custom_fan_mode_(want))
        changed = true;
    }
  }

  const auto preset = boost ? climate::CLIMATE_PRESET_BOOST : climate::CLIMATE_PRESET_NONE;
  if (!this->preset.has_value() || *this->preset != preset) {
    this->preset = preset;
    changed = true;
  }

  if (changed)
    this->publish_state();
}

}  // namespace esphome::flexit_sl4r
