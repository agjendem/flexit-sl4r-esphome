#include "boost_button.h"

namespace esphome::flexit_sl4r {

void BoostButton::press_action() { this->parent_->trigger_boost(); }

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

void DumpBootCaptureButton::press_action() { this->parent_->dump_boot_capture(); }

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

void DumpAnomaliesButton::press_action() { this->parent_->dump_anomalies(); }

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

void ResetFilterButton::press_action() { this->parent_->reset_filter_timer(); }

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

void CancelBoostButton::press_action() { this->parent_->cancel_boost(); }

}  // namespace esphome::flexit_sl4r
