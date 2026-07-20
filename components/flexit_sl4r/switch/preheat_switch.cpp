#include "preheat_switch.h"

namespace esphome::flexit_sl4r {

void PreheatSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->set_preheat(state);
}

}  // namespace esphome::flexit_sl4r
