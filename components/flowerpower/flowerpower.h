#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <deque>
#include <string>
#include <vector>

namespace esphome {
namespace flowerpower {

// Service live : 39e1FA00-84a8-11e2-afba-0002a5d5c51b
static const char *const LIVE_SVC = "39e1fa00-84a8-11e2-afba-0002a5d5c51b";
static const char *const UPLOAD_SVC = "39e1fb00-84a8-11e2-afba-0002a5d5c51b";
static const char *const HISTORY_SVC = "39e1fc00-84a8-11e2-afba-0002a5d5c51b";
static const char *const CLOCK_SVC = "39e1fd00-84a8-11e2-afba-0002a5d5c51b";
static const char *const WATER_SVC = "39e1f900-84a8-11e2-afba-0002a5d5c51b";

class FlowerPower : public PollingComponent, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_fetch_history(bool v) { this->fetch_history_ = v; }
  void set_history_publish_limit(uint16_t v) { this->history_publish_limit_ = v; }
  void set_air_temperature_sensor(sensor::Sensor *s) { this->air_temperature_ = s; }
  void set_soil_temperature_sensor(sensor::Sensor *s) { this->soil_temperature_ = s; }
  void set_soil_moisture_sensor(sensor::Sensor *s) { this->soil_moisture_ = s; }
  void set_sunlight_sensor(sensor::Sensor *s) { this->sunlight_ = s; }
  void set_soil_ec_sensor(sensor::Sensor *s) { this->soil_ec_ = s; }
  void set_battery_sensor(sensor::Sensor *s) { this->battery_ = s; }
  void set_history_entries_sensor(sensor::Sensor *s) { this->history_entries_ = s; }
  void set_session_period_sensor(sensor::Sensor *s) { this->session_period_ = s; }
  void set_firmware_sensor(text_sensor::TextSensor *s) { this->firmware_ = s; }
  void set_history_sample_sensor(text_sensor::TextSensor *s) { this->history_sample_ = s; }
  void set_water_level_sensor(sensor::Sensor *s) { this->water_level_ = s; }
  void set_watering_status_sensor(sensor::Sensor *s) { this->watering_status_ = s; }
  void set_watering_mode_sensor(sensor::Sensor *s) { this->watering_mode_ = s; }

  void set_led(bool on);
  void water_now(uint8_t seconds = 1);
  void sync_now();

 protected:
  sensor::Sensor *air_temperature_{nullptr};
  sensor::Sensor *soil_temperature_{nullptr};
  sensor::Sensor *soil_moisture_{nullptr};
  sensor::Sensor *sunlight_{nullptr};
  sensor::Sensor *soil_ec_{nullptr};
  sensor::Sensor *battery_{nullptr};
  sensor::Sensor *history_entries_{nullptr};
  sensor::Sensor *session_period_{nullptr};
  text_sensor::TextSensor *firmware_{nullptr};
  text_sensor::TextSensor *history_sample_{nullptr};
  sensor::Sensor *water_level_{nullptr};
  sensor::Sensor *watering_status_{nullptr};
  sensor::Sensor *watering_mode_{nullptr};

  bool fetch_history_{true};
  uint16_t history_publish_limit_{500};
  bool busy_{false};
  bool led_pending_{false};
  bool led_value_{false};
  bool water_pending_{false};
  uint8_t water_seconds_{1};
  uint32_t last_activity_{0};
  uint32_t clock_s_{0};
  uint16_t nb_entries_{0};
  uint32_t last_index_{0};
  uint16_t session_id_{0};
  uint16_t session_period_s_{0};
  uint32_t last_synced_index_{0};
  ESPPreferenceObject pref_;

  uint16_t h_battery_{0};
  uint16_t h_air_cal_{0};
  uint16_t h_air_raw_{0};
  uint16_t h_soil_raw_{0};
  uint16_t h_vwc_cal_{0};
  uint16_t h_vwc_raw_{0};
  uint16_t h_dli_cal_{0};
  uint16_t h_light_raw_{0};
  uint16_t h_ecb_cal_{0};
  uint16_t h_ec_raw_{0};
  uint16_t h_clock_{0};
  uint16_t h_nb_{0};
  uint16_t h_last_{0};
  uint16_t h_session_{0};
  uint16_t h_period_{0};
  uint16_t h_start_{0};
  uint16_t h_fw_{0};
  uint16_t h_led_{0};
  uint16_t h_tx_{0};
  uint16_t h_tx_status_{0};
  uint16_t h_rx_{0};
  uint16_t h_water_level_{0};
  uint16_t h_water_trigger_{0};
  uint16_t h_water_status_{0};
  uint16_t h_water_mode_{0};

  uint8_t read_step_{0};
  uint8_t notify_ready_{0};
  uint32_t file_length_{0};
  uint16_t frames_in_group_{0};
  uint8_t nack_count_{0};
  std::vector<uint8_t> file_;
  std::vector<std::vector<uint8_t>> group_;
  std::deque<std::string> publish_q_;

  void resolve_handles_();
  void kick_reads_();
  void read_handle_(uint16_t handle);
  void write_u8_(uint16_t handle, uint8_t v);
  void write_u32_le_(uint16_t handle, uint32_t v);
  void on_read_(uint16_t handle, const uint8_t *data, uint16_t len);
  void on_notify_(uint16_t handle, const uint8_t *data, uint16_t len);
  void start_history_();
  void ack_(bool ok);
  void parse_and_queue_();
  void finish_(bool ok);
  void publish_(sensor::Sensor *s, float v);
  static float raw_temperature_(uint16_t raw);
  static float raw_moisture_(uint16_t raw);
  static float raw_sunlight_(uint16_t raw);
  static float hist_temperature_(uint16_t raw);
  static float hist_moisture_(uint16_t raw);
  static float hist_light_(uint16_t raw);
  static float hist_battery_(uint16_t raw);
};

}  // namespace flowerpower
}  // namespace esphome
