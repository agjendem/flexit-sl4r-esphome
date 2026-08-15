#include "flexit_climate.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome::flexit_sl4r {

// The fan levels as custom fan modes. These must be static const char* -
// ESPHome stores the pointer, not a copy, and compares identity as well as
// content.
//
// The names come from Flexit's own CI 50 manual (110191N-07 p. 5), which
// describes each level in plain words:
//   level 1 - "lower ventilation demand than normal. Not to be used while the
//             home is occupied"
//   level 2 - "normal operating ventilation. This is the everyday setting"
//   level 3 - "increased ventilation in wet rooms", e.g. while showering
// "Increased" is used rather than the manual's "forced" for level 3, because
// "Boost" is already the name of the TIMED maximum function (the BOOST preset).
// The `select` entity keeps the plain numbers 1/2/3 - it mirrors the panel's
// three indicator LEDs directly.
//
// Translators: these strings are what the user sees in the Home Assistant fan
// mode dropdown. Adapt them to your language; nothing else depends on the
// exact wording.
static const char *const FAN_MODE_1 = "Reduced";
static const char *const FAN_MODE_2 = "Normal";
static const char *const FAN_MODE_3 = "Increased";

climate::ClimateTraits FlexitClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  // No OFF mode: the unit cannot be stopped over the bus. HEAT/FAN_ONLY
  // distinguishes whether the afterheater is enabled.
  traits.set_supported_modes({climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY});
  // BOOST = "max fan" (showering, cooking). The unit falls back to the return
  // level by itself when the period ends.
  traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_BOOST});
  traits.set_visual_min_temperature(15);
  traits.set_visual_max_temperature(25);
  traits.set_visual_target_temperature_step(1);
  traits.set_visual_current_temperature_step(0.1f);
  return traits;
}

void FlexitClimate::setup_state() {
  // Fan modes live on the entity (not traits) as of ESPHome 2026.5.
  this->set_supported_custom_fan_modes({FAN_MODE_1, FAN_MODE_2, FAN_MODE_3});
}

void FlexitClimate::control(const climate::ClimateCall &call) {
  // The order is deliberate: boost is handled last, so that a simultaneous
  // level change in the same call does not cancel the boost we just asked for.
  if (call.get_target_temperature().has_value())
    this->parent_->set_heat_exchanger_setpoint(static_cast<uint8_t>(lroundf(*call.get_target_temperature())));

  if (call.get_mode().has_value())
    this->parent_->set_afterheat_enabled(*call.get_mode() == climate::CLIMATE_MODE_HEAT);

  const auto fm = call.get_custom_fan_mode();
  if (!fm.empty()) {
    uint8_t level = 0;
    if (strcmp(fm.c_str(), FAN_MODE_1) == 0) {
      level = 1;
    } else if (strcmp(fm.c_str(), FAN_MODE_2) == 0) {
      level = 2;
    } else if (strcmp(fm.c_str(), FAN_MODE_3) == 0) {
      level = 3;
    }
    if (level != 0)
      this->parent_->set_fan_level(level);
  }

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

  // HEATING when heat demand is above zero, i.e. when the unit is actually
  // calling for heat (rotor and possibly the afterheater).
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
