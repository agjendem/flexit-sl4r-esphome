#pragma once

#include "esphome/components/select/select.h"
#include "../flexit_sl4r.h"

namespace esphome::flexit_sl4r {

class FanLevelSelect final : public select::Select, public Parented<FlexitSL4RComponent> {
 public:
  FanLevelSelect() = default;

 protected:
  void control(size_t index) override;
};

}  // namespace esphome::flexit_sl4r
