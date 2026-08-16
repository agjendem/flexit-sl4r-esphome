// SPDX-License-Identifier: GPL-3.0-or-later
// Derivative work of ESPHome's GPLv3 runtime. See LICENSE.
#include "flexit_climate.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome::flexit_sl4r {

// The three fan levels are exposed as Home Assistant's STANDARD fan modes
// rather than as custom strings, because only the standard ones CAN be
// translated. A custom string is shown verbatim to every user regardless of
// their language, so any wording chosen here would be wrong for most of them.
//
// Whether a standard mode actually appears translated is Home Assistant's
// business, not ours, and the catalogue has gaps: Norwegian renders "Low"
// untranslated as of 2026.8 because `common::state::low` has no nb entry, even
// though the rest of the dialog is translated. That resolves itself upstream;
// a custom string never would.
//
// The mapping to Flexit's own vocabulary, from the CI 50 manual
// (110191N-07 p. 5), which describes each level in plain words:
//   LOW    = level 1, "lower ventilation demand than normal. Not to be used
//            while the home is occupied"
//   MEDIUM = level 2, "normal operating ventilation. This is the everyday
//            setting"
//   HIGH   = level 3, "increased ventilation in wet rooms", e.g. while
//            showering
//
// Note that level 3 is NOT the same as the BOOST preset: level 3 is a
// permanent setting, boost is the timed maximum the unit backs out of by
// itself. The `select` entity keeps the plain numbers 1/2/3 - it mirrors the
// panel's three indicator LEDs directly, and digits need no translation.

climate::ClimateTraits FlexitClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});
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

void FlexitClimate::control(const climate::ClimateCall &call) {
  // The order is deliberate: boost is handled last, so that a simultaneous
  // level change in the same call does not cancel the boost we just asked for.
  if (call.get_target_temperature().has_value())
    this->parent_->set_heat_exchanger_setpoint(static_cast<uint8_t>(lroundf(*call.get_target_temperature())));

  if (call.get_mode().has_value())
    this->parent_->set_afterheat_enabled(*call.get_mode() == climate::CLIMATE_MODE_HEAT);

  if (call.get_fan_mode().has_value()) {
    uint8_t level = 0;
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:
        level = 1;
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        level = 2;
        break;
      case climate::CLIMATE_FAN_HIGH:
        level = 3;
        break;
      default:
        break;
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
    const auto want = fan_level == 1 ? climate::CLIMATE_FAN_LOW
                                     : (fan_level == 2 ? climate::CLIMATE_FAN_MEDIUM : climate::CLIMATE_FAN_HIGH);
    if (!this->fan_mode.has_value() || *this->fan_mode != want) {
      if (this->set_fan_mode_(want))
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
