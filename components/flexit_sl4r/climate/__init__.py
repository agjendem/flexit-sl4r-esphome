# SPDX-License-Identifier: MIT
# ESPHome codegen. See LICENSE.
import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent, flexit_sl4r_ns

DEPENDENCIES = ["flexit_sl4r"]

FlexitClimate = flexit_sl4r_ns.class_("FlexitClimate", climate.Climate, cg.Parented)

CONF_VENTILATION = "ventilation"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    cv.Optional(CONF_VENTILATION): climate.climate_schema(FlexitClimate),
}


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if ventilation_config := config.get(CONF_VENTILATION):
        clim = await climate.new_climate(ventilation_config)
        await cg.register_parented(clim, config[CONF_FLEXIT_SL4R_ID])
        cg.add(hub.set_ventilation_climate(clim))
