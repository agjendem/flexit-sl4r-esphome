# SPDX-License-Identifier: MIT
# ESPHome codegen. See LICENSE.
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
CONF_UNKNOWN_ALARM = "unknown_alarm"
CONF_HEAT_RECOVERY_ACTIVE = "heat_recovery_active"
CONF_AFTERHEATER_HEATING = "afterheater_heating"
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
    # Boost: high nibble != low nibble in payload[5]. A precise indicator,
    # free from data we already read.
    cv.Optional(CONF_BOOST_ACTIVE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING, icon="mdi:fan-plus"
    ),
    # Afterheater enabled - payload[6] bit7.
    cv.Optional(CONF_AFTERHEAT_ENABLED): binary_sensor.binary_sensor_schema(
        icon="mdi:radiator", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    # Filter time expired. The CS 50 has no pressure switch - the alarm is
    # time-based.
    cv.Optional(CONF_FILTER_ALARM): binary_sensor.binary_sensor_schema(
        device_class="problem", icon="mdi:air-filter"
    ),
    # Any alarm bit EXCEPT the filter bit. The red alarm LED (rotor alarm,
    # overheat thermostat) has not been decoded bit by bit - this catches it
    # anyway the day it fires. Which bit it was is read from the raw [4]
    # sensor.
    cv.Optional(CONF_UNKNOWN_ALARM): binary_sensor.binary_sensor_schema(
        device_class="problem", icon="mdi:alert-circle"
    ),
    cv.Optional(CONF_HEAT_RECOVERY_ACTIVE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING, icon="mdi:autorenew"
    ),
    # The element is drawing power right now, as opposed to merely being
    # permitted to - see PROTOCOL.md section 5.2.
    cv.Optional(CONF_AFTERHEATER_HEATING): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING, icon="mdi:heating-coil"
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
    if unknown_alarm_config := config.get(CONF_UNKNOWN_ALARM):
        sens = await binary_sensor.new_binary_sensor(unknown_alarm_config)
        cg.add(flexit_sl4r_component.set_unknown_alarm_binary_sensor(sens))
    if heat_recovery_config := config.get(CONF_HEAT_RECOVERY_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(heat_recovery_config)
        cg.add(flexit_sl4r_component.set_heat_recovery_active_binary_sensor(sens))
    if afterheater_heating_config := config.get(CONF_AFTERHEATER_HEATING):
        sens = await binary_sensor.new_binary_sensor(afterheater_heating_config)
        cg.add(flexit_sl4r_component.set_afterheater_heating_binary_sensor(sens))
    if enumerated_config := config.get(CONF_ENUMERATED):
        sens = await binary_sensor.new_binary_sensor(enumerated_config)
        cg.add(flexit_sl4r_component.set_enumerated_binary_sensor(sens))
