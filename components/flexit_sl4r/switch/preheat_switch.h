#pragma once

#include "esphome/components/switch/switch.h"
#include "../flexit_sl4r.h"

namespace esphome::flexit_sl4r {

class PreheatSwitch final : public switch_::Switch, public Parented<FlexitSL4RComponent> {
 public:
  PreheatSwitch() = default;

 protected:
  void write_state(bool state) override;
};

}  // namespace esphome::flexit_sl4r
