import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_RADIATOR,
)

from . import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent

DEPENDENCIES = ["flexit_sl4r"]

CONF_PREHEAT_ACTIVE = "preheat_active"
CONF_COMMUNICATION = "communication"
CONF_BOOST_ACTIVE = "boost_active"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    cv.Optional(CONF_PREHEAT_ACTIVE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING, icon=ICON_RADIATOR
    ),
    cv.Optional(CONF_COMMUNICATION): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_CONNECTIVITY,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    # Forsering: høy nibbel != lav nibbel i payload[5]. Presis indikator,
    # gratis fra data vi allerede leser.
    cv.Optional(CONF_BOOST_ACTIVE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING, icon="mdi:fan-plus"
    ),
}


async def to_code(config):
    flexit_sl4r_component = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if preheat_active_config := config.get(CONF_PREHEAT_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(preheat_active_config)
        cg.add(flexit_sl4r_component.set_preheat_active_binary_sensor(sens))
    if communication_config := config.get(CONF_COMMUNICATION):
        sens = await binary_sensor.new_binary_sensor(communication_config)
        cg.add(flexit_sl4r_component.set_communication_binary_sensor(sens))
    if boost_active_config := config.get(CONF_BOOST_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(boost_active_config)
        cg.add(flexit_sl4r_component.set_boost_active_binary_sensor(sens))
