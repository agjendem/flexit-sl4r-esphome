# SPDX-License-Identifier: MIT
# ESPHome codegen. See LICENSE.
import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_ID, ICON_FAN

from .. import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent, flexit_sl4r_ns

FanLevelSelect = flexit_sl4r_ns.class_("FanLevelSelect", select.Select)

CONF_FAN_LEVEL = "fan_level"

FAN_LEVEL_OPTIONS = ["1", "2", "3"]

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    cv.Optional(CONF_FAN_LEVEL): select.select_schema(FanLevelSelect, icon=ICON_FAN),
}


async def to_code(config):
    flexit_sl4r_component = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if fan_level_config := config.get(CONF_FAN_LEVEL):
        s = await select.new_select(fan_level_config, options=FAN_LEVEL_OPTIONS)
        await cg.register_parented(s, config[CONF_FLEXIT_SL4R_ID])
        cg.add(flexit_sl4r_component.set_fan_level_select(s))
