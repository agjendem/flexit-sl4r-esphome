import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_TEMPERATURE,
    ICON_THERMOMETER,
    UNIT_CELSIUS,
)

from .. import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent, flexit_sl4r_ns

HeatExchangerSetpointNumber = flexit_sl4r_ns.class_("HeatExchangerSetpointNumber", number.Number)

CONF_HEAT_EXCHANGER_SETPOINT = "heat_exchanger_setpoint"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    cv.Optional(CONF_HEAT_EXCHANGER_SETPOINT): number.number_schema(
        HeatExchangerSetpointNumber,
        device_class=DEVICE_CLASS_TEMPERATURE,
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
    ),
}


async def to_code(config):
    flexit_sl4r_component = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if setpoint_config := config.get(CONF_HEAT_EXCHANGER_SETPOINT):
        # Valid range 15-25 degrees, see PROTOCOL.md.
        n = await number.new_number(setpoint_config, min_value=15, max_value=25, step=1)
        await cg.register_parented(n, config[CONF_FLEXIT_SL4R_ID])
        cg.add(flexit_sl4r_component.set_heat_exchanger_setpoint_number(n))
