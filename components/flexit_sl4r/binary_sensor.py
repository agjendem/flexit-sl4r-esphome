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

CONF_AFTERHEAT_ACTIVE = "afterheat_active"
CONF_AFTERHEAT_ENABLED = "afterheat_enabled"
CONF_FILTER_ALARM = "filter_alarm"
CONF_HEAT_RECOVERY_ACTIVE = "heat_recovery_active"
CONF_BYPASS_ACTIVE = "bypass_active"
CONF_COMMUNICATION = "communication"
CONF_BOOST_ACTIVE = "boost_active"
CONF_ENUMERATED = "enumerated"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    cv.Optional(CONF_AFTERHEAT_ACTIVE): binary_sensor.binary_sensor_schema(
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
    # Uten denne feiler skriving STILLE hvis vi er droppet fra pollerunden.
    cv.Optional(CONF_AFTERHEAT_ENABLED): binary_sensor.binary_sensor_schema(
        icon="mdi:radiator", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    # Filtertid utlopt. CS 50 har ingen trykkvakt - alarmen er tidsbasert.
    cv.Optional(CONF_FILTER_ALARM): binary_sensor.binary_sensor_schema(
        device_class="problem", icon="mdi:air-filter"
    ),
    cv.Optional(CONF_HEAT_RECOVERY_ACTIVE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING, icon="mdi:autorenew"
    ),
    # ANTATT bypass. Aldri observert satt - se protocol-notes.md.
    cv.Optional(CONF_BYPASS_ACTIVE): binary_sensor.binary_sensor_schema(
        icon="mdi:valve", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    cv.Optional(CONF_ENUMERATED): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_CONNECTIVITY,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}


async def to_code(config):
    flexit_sl4r_component = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if afterheat_active_config := config.get(CONF_AFTERHEAT_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(afterheat_active_config)
        cg.add(flexit_sl4r_component.set_afterheat_active_binary_sensor(sens))
    if communication_config := config.get(CONF_COMMUNICATION):
        sens = await binary_sensor.new_binary_sensor(communication_config)
        cg.add(flexit_sl4r_component.set_communication_binary_sensor(sens))
    if boost_active_config := config.get(CONF_BOOST_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(boost_active_config)
        cg.add(flexit_sl4r_component.set_boost_active_binary_sensor(sens))
    if afterheat_enabled_config := config.get(CONF_AFTERHEAT_ENABLED):
        sens = await binary_sensor.new_binary_sensor(afterheat_enabled_config)
        cg.add(flexit_sl4r_component.set_afterheat_enabled_binary_sensor(sens))
    if filter_alarm_config := config.get(CONF_FILTER_ALARM):
        sens = await binary_sensor.new_binary_sensor(filter_alarm_config)
        cg.add(flexit_sl4r_component.set_filter_alarm_binary_sensor(sens))
    if heat_recovery_config := config.get(CONF_HEAT_RECOVERY_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(heat_recovery_config)
        cg.add(flexit_sl4r_component.set_heat_recovery_active_binary_sensor(sens))
    if bypass_config := config.get(CONF_BYPASS_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(bypass_config)
        cg.add(flexit_sl4r_component.set_bypass_active_binary_sensor(sens))
    if enumerated_config := config.get(CONF_ENUMERATED):
        sens = await binary_sensor.new_binary_sensor(enumerated_config)
        cg.add(flexit_sl4r_component.set_enumerated_binary_sensor(sens))
