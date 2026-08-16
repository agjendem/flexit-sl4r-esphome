#pragma once

#include "esphome/components/climate/climate.h"
#include "../flexit_sl4r.h"

namespace esphome::flexit_sl4r {

// Gathers setpoint, fan level, boost and afterheater into a single thermostat
// model, the way Home Assistant's own Flexit integration (flexit_bacnet) does.
// The discrete entities remain alongside it - they are better for automations,
// while this gives one card with everything on it.
//
// The mode choice reflects what the unit can ACTUALLY do: it cannot be stopped
// over the bus, so there is no OFF. HEAT = the afterheater is enabled (the
// element is allowed to heat), FAN_ONLY = heat recovery only.
class FlexitClimate final : public climate::Climate, public Parented<FlexitSL4RComponent> {
 public:
  // Called from the hub when something on the bus has changed.
  void publish_from_bus(float current_temp, uint8_t setpoint, uint8_t fan_level, bool boost, bool afterheat_enabled,
                        bool heating);

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
};

}  // namespace esphome::flexit_sl4r
