import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, ICON_RADIATOR

from .. import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent, flexit_sl4r_ns

PreheatSwitch = flexit_sl4r_ns.class_("PreheatSwitch", switch.Switch)

CONF_PREHEAT = "preheat"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    cv.Optional(CONF_PREHEAT): switch.switch_schema(PreheatSwitch, icon=ICON_RADIATOR),
}


async def to_code(config):
    flexit_sl4r_component = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if preheat_config := config.get(CONF_PREHEAT):
        s = await switch.new_switch(preheat_config)
        await cg.register_parented(s, config[CONF_FLEXIT_SL4R_ID])
        cg.add(flexit_sl4r_component.set_preheat_switch(s))
