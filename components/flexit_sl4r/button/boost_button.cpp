#include "boost_button.h"

namespace esphome::flexit_sl4r {

void BoostButton::press_action() { this->parent_->trigger_boost(); }

}  // namespace esphome::flexit_sl4r
