# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 JC-23

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_TEMPERATURE,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_SECOND,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

CONF_FIRMWARE = "firmware"
CONF_MODE = "mode"
CONF_STEAM_TEMPERATURE = "steam_temperature"
CONF_TARGET_STEAM_TEMPERATURE = "target_steam_temperature"
CONF_HX_TEMPERATURE = "hx_temperature"
CONF_BOOST_COUNTDOWN = "boost_countdown"
CONF_HEATING = "heating"
CONF_PUMP = "pump"
CONF_SHOT_COUNT = "shot_count"

marax_ns = cg.esphome_ns.namespace("marax")
MaraXComponent = marax_ns.class_(
    "MaraXComponent", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MaraXComponent),
            cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Required(CONF_FIRMWARE): text_sensor.text_sensor_schema(
                icon="mdi:chip", entity_category="diagnostic"
            ),
            cv.Required(CONF_MODE): text_sensor.text_sensor_schema(
                icon="mdi:coffee-maker"
            ),
            cv.Required(CONF_STEAM_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=0,
            ),
            cv.Required(CONF_TARGET_STEAM_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=0,
            ),
            cv.Required(CONF_HX_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=0,
            ),
            cv.Required(CONF_BOOST_COUNTDOWN): sensor.sensor_schema(
                unit_of_measurement=UNIT_SECOND,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=0,
            ),
            cv.Required(CONF_HEATING): binary_sensor.binary_sensor_schema(
                icon="mdi:radiator"
            ),
            cv.Required(CONF_PUMP): binary_sensor.binary_sensor_schema(
                icon="mdi:pump"
            ),
            cv.Required(CONF_SHOT_COUNT): sensor.sensor_schema(
                unit_of_measurement="shots",
                icon=ICON_COUNTER,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                accuracy_decimals=0,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for key, setter, factory in (
        (CONF_FIRMWARE, "set_firmware_sensor", text_sensor.new_text_sensor),
        (CONF_MODE, "set_mode_sensor", text_sensor.new_text_sensor),
        (CONF_STEAM_TEMPERATURE, "set_steam_sensor", sensor.new_sensor),
        (
            CONF_TARGET_STEAM_TEMPERATURE,
            "set_target_steam_sensor",
            sensor.new_sensor,
        ),
        (CONF_HX_TEMPERATURE, "set_hx_sensor", sensor.new_sensor),
        (CONF_BOOST_COUNTDOWN, "set_boost_sensor", sensor.new_sensor),
        (CONF_HEATING, "set_heating_sensor", binary_sensor.new_binary_sensor),
        (CONF_PUMP, "set_pump_sensor", binary_sensor.new_binary_sensor),
        (CONF_SHOT_COUNT, "set_shot_count_sensor", sensor.new_sensor),
    ):
        sens = await factory(config[key])
        cg.add(getattr(var, setter)(sens))
