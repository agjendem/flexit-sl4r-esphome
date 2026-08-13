import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from .. import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent, flexit_sl4r_ns

DEPENDENCIES = ["flexit_sl4r"]

CONF_BOOST = "boost"

BoostButton = flexit_sl4r_ns.class_("BoostButton", button.Button, cg.Parented)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
        cv.Optional(CONF_BOOST): button.button_schema(BoostButton),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if boost_config := config.get(CONF_BOOST):
        btn = await button.new_button(boost_config)
        await cg.register_parented(btn, parent)
