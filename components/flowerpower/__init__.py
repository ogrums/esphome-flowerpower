"""Composant ESPHome : Parrot Flower Power (BLE)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_MOISTURE,
    DEVICE_CLASS_TEMPERATURE,
    ICON_BLUETOOTH,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_EMPTY,
    UNIT_PERCENT,
    UNIT_SECOND,
)

DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "text_sensor"]

flowerpower_ns = cg.esphome_ns.namespace("flowerpower")
FlowerPower = flowerpower_ns.class_(
    "FlowerPower", cg.PollingComponent, ble_client.BLEClientNode
)

CONF_AIR_TEMPERATURE = "air_temperature"
CONF_SOIL_TEMPERATURE = "soil_temperature"
CONF_SOIL_MOISTURE = "soil_moisture"
CONF_SUNLIGHT = "sunlight"
CONF_SOIL_EC = "soil_ec"
CONF_BATTERY = "battery_level"
CONF_HISTORY_ENTRIES = "history_entries"
CONF_SESSION_PERIOD = "session_period"
CONF_FIRMWARE = "firmware_revision"
CONF_HISTORY_SAMPLE = "history_sample"
CONF_FETCH_HISTORY = "fetch_history"
CONF_HISTORY_PUBLISH_LIMIT = "history_publish_limit"

def _sensor(unit, decimals, device_class=None, icon=None):
    kwargs = dict(
        unit_of_measurement=unit,
        accuracy_decimals=decimals,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    if device_class is not None:
        kwargs["device_class"] = device_class
    if icon is not None:
        kwargs["icon"] = icon
    return sensor.sensor_schema(**kwargs)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlowerPower),
            cv.Optional(CONF_FETCH_HISTORY, default=True): cv.boolean,
            cv.Optional(CONF_HISTORY_PUBLISH_LIMIT, default=500): cv.int_range(min=0, max=5000),
            cv.Optional(CONF_AIR_TEMPERATURE): _sensor(UNIT_CELSIUS, 1, DEVICE_CLASS_TEMPERATURE),
            cv.Optional(CONF_SOIL_TEMPERATURE): _sensor(UNIT_CELSIUS, 1, DEVICE_CLASS_TEMPERATURE),
            cv.Optional(CONF_SOIL_MOISTURE): _sensor(UNIT_PERCENT, 1, DEVICE_CLASS_MOISTURE),
            cv.Optional(CONF_SUNLIGHT): _sensor("mol/m²/d", 2, icon="mdi:white-balance-sunny"),
            cv.Optional(CONF_SOIL_EC): _sensor("dS/m", 3, icon="mdi:flash"),
            cv.Optional(CONF_BATTERY): _sensor(UNIT_PERCENT, 0, DEVICE_CLASS_BATTERY),
            cv.Optional(CONF_HISTORY_ENTRIES): _sensor(UNIT_EMPTY, 0, icon="mdi:database"),
            cv.Optional(CONF_SESSION_PERIOD): _sensor(UNIT_SECOND, 0, DEVICE_CLASS_DURATION),
            cv.Optional(CONF_FIRMWARE): text_sensor.text_sensor_schema(icon="mdi:chip"),
            cv.Optional(CONF_HISTORY_SAMPLE): text_sensor.text_sensor_schema(
                icon="mdi:timeline-clock-outline"
            ),
        }
    )
    .extend(cv.polling_component_schema("30min"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_fetch_history(config[CONF_FETCH_HISTORY]))
    cg.add(var.set_history_publish_limit(config[CONF_HISTORY_PUBLISH_LIMIT]))

    mapping = [
        (CONF_AIR_TEMPERATURE, var.set_air_temperature_sensor, sensor.new_sensor),
        (CONF_SOIL_TEMPERATURE, var.set_soil_temperature_sensor, sensor.new_sensor),
        (CONF_SOIL_MOISTURE, var.set_soil_moisture_sensor, sensor.new_sensor),
        (CONF_SUNLIGHT, var.set_sunlight_sensor, sensor.new_sensor),
        (CONF_SOIL_EC, var.set_soil_ec_sensor, sensor.new_sensor),
        (CONF_BATTERY, var.set_battery_sensor, sensor.new_sensor),
        (CONF_HISTORY_ENTRIES, var.set_history_entries_sensor, sensor.new_sensor),
        (CONF_SESSION_PERIOD, var.set_session_period_sensor, sensor.new_sensor),
        (CONF_FIRMWARE, var.set_firmware_sensor, text_sensor.new_text_sensor),
        (CONF_HISTORY_SAMPLE, var.set_history_sample_sensor, text_sensor.new_text_sensor),
    ]
    for key, setter, factory in mapping:
        if key in config:
            sens = await factory(config[key])
            cg.add(setter(sens))
