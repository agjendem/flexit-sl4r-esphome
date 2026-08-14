import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from .. import CONF_FLEXIT_SL4R_ID, FlexitSL4RComponent, flexit_sl4r_ns

DEPENDENCIES = ["flexit_sl4r"]

CONF_BOOST = "boost"
CONF_DUMP_BOOT_CAPTURE = "dump_boot_capture"
CONF_DUMP_ANOMALIES = "dump_anomalies"

BoostButton = flexit_sl4r_ns.class_("BoostButton", button.Button, cg.Parented)
DumpBootCaptureButton = flexit_sl4r_ns.class_("DumpBootCaptureButton", button.Button, cg.Parented)
DumpAnomaliesButton = flexit_sl4r_ns.class_("DumpAnomaliesButton", button.Button, cg.Parented)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
        cv.Optional(CONF_BOOST): button.button_schema(BoostButton),
        cv.Optional(CONF_DUMP_BOOT_CAPTURE): button.button_schema(DumpBootCaptureButton),
        cv.Optional(CONF_DUMP_ANOMALIES): button.button_schema(DumpAnomaliesButton),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FLEXIT_SL4R_ID])
    if boost_config := config.get(CONF_BOOST):
        btn = await button.new_button(boost_config)
        await cg.register_parented(btn, parent)
    if dump_config := config.get(CONF_DUMP_BOOT_CAPTURE):
        btn = await button.new_button(dump_config)
        await cg.register_parented(btn, parent)
    if anomaly_config := config.get(CONF_DUMP_ANOMALIES):
        btn = await button.new_button(anomaly_config)
        await cg.register_parented(btn, parent)
