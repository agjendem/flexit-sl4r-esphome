#pragma once

#include "esphome/components/number/number.h"
#include "../flexit_sl4r.h"

namespace esphome::flexit_sl4r {

class HeatExchangerSetpointNumber final : public number::Number, public Parented<FlexitSL4RComponent> {
 public:
  HeatExchangerSetpointNumber() = default;

 protected:
  void control(float value) override;
};

}  // namespace esphome::flexit_sl4r
