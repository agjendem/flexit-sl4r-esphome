import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INDEX,
    CONF_NAME,
    CONF_TYPE,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from . import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent

DEPENDENCIES = ["flexit_sl4r"]

CONF_SUPPLY_AIR_TEMPERATURE = "supply_air_temperature"
CONF_HEAT_EXCHANGER_SETPOINT_RAW = "heat_exchanger_setpoint_raw"
CONF_FAN_DUTY_SUPPLY = "fan_duty_supply"
CONF_FAN_DUTY_EXTRACT = "fan_duty_extract"
CONF_FAN_LEVEL_RUNNING = "fan_level_running"
CONF_FAN_LEVEL_RETURN = "fan_level_return"
CONF_FRAMES_DISCARDED = "frames_discarded"
CONF_STATUS_INTERVAL = "status_interval"
CONF_ANOMALIES = "anomalies"
CONF_RAW_STATUS_BYTES = "raw_status_bytes"
CONF_FLOAT_REGISTERS = "float_registers"
CONF_REGISTER = "register"
CONF_SLOT = "slot"


def _temperature_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def _percent_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def _diagnostic_schema():
    return sensor.sensor_schema(
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


# Utforsknings-sensorer: eksponer vilkårlige rå byte / flyttall-slots i HA uten
# å endre C++-koden, slik at recorder-en bygger historikk å korrelere mot.
RAW_STATUS_BYTE_SCHEMA = _diagnostic_schema().extend(
    {cv.Required(CONF_INDEX): cv.int_range(min=0, max=21)}
)

FLOAT_REGISTER_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=2,
    state_class=STATE_CLASS_MEASUREMENT,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.Optional(CONF_TYPE, default=0xC2): cv.hex_int_range(min=0xC0, max=0xC7),
        cv.Required(CONF_REGISTER): cv.int_range(min=0, max=255),
        cv.Required(CONF_SLOT): cv.int_range(min=0, max=6),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
        cv.Optional(CONF_SUPPLY_AIR_TEMPERATURE): _temperature_schema(),
        cv.Optional(CONF_HEAT_EXCHANGER_SETPOINT_RAW): _temperature_schema().extend(
            {cv.Optional("entity_category", default="diagnostic"): cv.entity_category}
        ),
        cv.Optional(CONF_FAN_DUTY_SUPPLY): _percent_schema(),
        cv.Optional(CONF_FAN_DUTY_EXTRACT): _percent_schema(),
        cv.Optional(CONF_FAN_LEVEL_RUNNING): _diagnostic_schema(),
        cv.Optional(CONF_FAN_LEVEL_RETURN): _diagnostic_schema(),
        cv.Optional(CONF_FRAMES_DISCARDED): _diagnostic_schema(),
        cv.Optional(CONF_ANOMALIES): _diagnostic_schema(),
        cv.Optional(CONF_STATUS_INTERVAL): sensor.sensor_schema(
            unit_of_measurement="s",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RAW_STATUS_BYTES): cv.ensure_list(RAW_STATUS_BYTE_SCHEMA),
        cv.Optional(CONF_FLOAT_REGISTERS): cv.ensure_list(FLOAT_REGISTER_SCHEMA),
    }
)

_SIMPLE = {
    CONF_SUPPLY_AIR_TEMPERATURE: "set_supply_air_temperature_sensor",
    CONF_HEAT_EXCHANGER_SETPOINT_RAW: "set_heat_exchanger_setpoint_raw_sensor",
    CONF_FAN_DUTY_SUPPLY: "set_fan_duty_supply_sensor",
    CONF_FAN_DUTY_EXTRACT: "set_fan_duty_extract_sensor",
    CONF_FAN_LEVEL_RUNNING: "set_fan_level_running_sensor",
    CONF_FAN_LEVEL_RETURN: "set_fan_level_return_sensor",
    CONF_FRAMES_DISCARDED: "set_frames_discarded_sensor",
    CONF_STATUS_INTERVAL: "set_status_interval_sensor",
    CONF_ANOMALIES: "set_anomalies_sensor",
}


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])

    for key, setter in _SIMPLE.items():
        if sub := config.get(key):
            sens = await sensor.new_sensor(sub)
            cg.add(getattr(hub, setter)(sens))

    for sub in config.get(CONF_RAW_STATUS_BYTES, []):
        sens = await sensor.new_sensor(sub)
        cg.add(hub.add_raw_status_sensor(sub[CONF_INDEX], sens))

    for sub in config.get(CONF_FLOAT_REGISTERS, []):
        sens = await sensor.new_sensor(sub)
        cg.add(
            hub.add_float_register_sensor(
                sub[CONF_TYPE], sub[CONF_REGISTER], sub[CONF_SLOT], sens
            )
        )
