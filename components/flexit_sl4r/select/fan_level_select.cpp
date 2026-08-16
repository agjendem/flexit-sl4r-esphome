// SPDX-License-Identifier: GPL-3.0-or-later
// Derivative work of ESPHome's GPLv3 runtime. See LICENSE.
#include "fan_level_select.h"

namespace esphome::flexit_sl4r {

void FanLevelSelect::control(size_t index) {
  this->publish_state(index);
  this->parent_->set_fan_level(static_cast<uint8_t>(index + 1));
}

}  // namespace esphome::flexit_sl4r
