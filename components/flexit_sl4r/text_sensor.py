# SPDX-License-Identifier: MIT
# ESPHome codegen. See LICENSE.
import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent

DEPENDENCIES = ["flexit_sl4r"]

CONF_CONTROLLER_FIRMWARE = "controller_firmware"
CONF_PANEL_FIRMWARE = "panel_firmware"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
        cv.Optional(CONF_CONTROLLER_FIRMWARE): text_sensor.text_sensor_schema(
            icon="mdi:chip", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_PANEL_FIRMWARE): text_sensor.text_sensor_schema(
            icon="mdi:remote", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if sub := config.get(CONF_CONTROLLER_FIRMWARE):
        sens = await text_sensor.new_text_sensor(sub)
        cg.add(hub.set_controller_firmware_text_sensor(sens))
    if sub := config.get(CONF_PANEL_FIRMWARE):
        sens = await text_sensor.new_text_sensor(sub)
        cg.add(hub.set_panel_firmware_text_sensor(sens))
