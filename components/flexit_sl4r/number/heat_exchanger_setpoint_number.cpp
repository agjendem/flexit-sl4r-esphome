#include "heat_exchanger_setpoint_number.h"

namespace esphome::flexit_sl4r {

void HeatExchangerSetpointNumber::control(float value) {
  this->publish_state(value);
  this->parent_->set_heat_exchanger_setpoint(static_cast<uint8_t>(value));
}

}  // namespace esphome::flexit_sl4r
