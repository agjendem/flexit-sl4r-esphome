// SPDX-License-Identifier: GPL-3.0-or-later
// Derivative work of ESPHome's GPLv3 runtime. See LICENSE.
#pragma once

#include "esphome/components/button/button.h"
#include "../flexit_sl4r.h"

namespace esphome::flexit_sl4r {

class BoostButton final : public button::Button, public Parented<FlexitSL4RComponent> {
 public:
  BoostButton() = default;

 protected:
  void press_action() override;
};

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

class DumpBootCaptureButton final : public button::Button, public Parented<FlexitSL4RComponent> {
 protected:
  void press_action() override;
};

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

class DumpAnomaliesButton final : public button::Button, public Parented<FlexitSL4RComponent> {
 protected:
  void press_action() override;
};

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

class ResetFilterButton final : public button::Button, public Parented<FlexitSL4RComponent> {
 protected:
  void press_action() override;
};

}  // namespace esphome::flexit_sl4r

namespace esphome::flexit_sl4r {

class CancelBoostButton final : public button::Button, public Parented<FlexitSL4RComponent> {
 protected:
  void press_action() override;
};

}  // namespace esphome::flexit_sl4r
