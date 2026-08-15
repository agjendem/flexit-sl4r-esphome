#pragma once

#include "esphome/components/climate/climate.h"
#include "../flexit_sl4r.h"

namespace esphome::flexit_sl4r {

// Samler settpunkt, viftetrinn, forsering og ettervarme i én termostat-modell,
// slik HAs egen Flexit-integrasjon (flexit_bacnet) også gjør. De diskrete
// entitetene beholdes ved siden av — de er bedre for automasjoner, mens denne
// gir ett kort med alt i.
//
// Modusvalget speiler hva aggregatet FAKTISK kan: det kan ikke stoppes over
// bussen, så det finnes ingen OFF. HEAT = ettervarmen er aktivert (elementet
// får lov å varme), FAN_ONLY = kun gjenvinning.
class FlexitClimate final : public climate::Climate, public Parented<FlexitSL4RComponent> {
 public:
  // Registrerer viftemodusene. Fra ESPHome 2026.5 ligger custom fan modes på
  // entiteten, ikke i traits.
  void setup_state();
  // Kalles fra hub-en når noe har endret seg på bussen.
  void publish_from_bus(float current_temp, uint8_t setpoint, uint8_t fan_level, bool boost, bool afterheat_enabled,
                        bool heating);

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
};

}  // namespace esphome::flexit_sl4r
