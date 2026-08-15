import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID, CONF_FLOW_CONTROL_PIN
from esphome.cpp_helpers import gpio_pin_expression

CODEOWNERS = ["@agjendem"]
DEPENDENCIES = ["uart"]
MULTI_CONF = False

flexit_sl4r_ns = cg.esphome_ns.namespace("flexit_sl4r")
FlexitSL4RComponent = flexit_sl4r_ns.class_("FlexitSL4RComponent", cg.Component, uart.UARTDevice)

CONF_FLEXIT_SL4R_ID = "flexit_sl4r_id"
CONF_SOURCE_NODE = "source_node"
CONF_RESPOND_TO_POLLS = "respond_to_polls"


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlexitSL4RComponent),
            cv.Optional(CONF_SOURCE_NODE, default=4): cv.int_range(min=1, max=8),
            cv.Optional(CONF_RESPOND_TO_POLLS, default=False): cv.boolean,
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "flexit_sl4r",
    baud_rate=19200,
    require_tx=True,
    require_rx=True,
    parity="NONE",
    stop_bits=1,
)

# Used by the child platforms (select/switch/number/binary_sensor) to reference
# the hub component. The CS50 sends data fast enough that the default 64-byte
# RX buffer overflows within seconds, so rx_buffer_size in the uart: block MUST
# be set generously - see the example configuration.
FLEXIT_SL4R_CLIENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FLEXIT_SL4R_ID): cv.use_id(FlexitSL4RComponent),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)


    if flow_control_pin_config := config.get(CONF_FLOW_CONTROL_PIN):
        pin = await gpio_pin_expression(flow_control_pin_config)
        cg.add(var.set_flow_control_pin(pin))
    cg.add(var.set_source_node(config[CONF_SOURCE_NODE]))
    cg.add(var.set_respond_to_polls(config[CONF_RESPOND_TO_POLLS]))
