#include "flowerpower.h"

#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace esphome {
namespace flowerpower {

static const char *const TAG = "flowerpower";

static uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
static uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint16_t be16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static float f32le(const uint8_t *p) {
  float v;
  memcpy(&v, p, sizeof(v));
  return v;
}

void FlowerPower::setup() {
  uint32_t key = static_cast<uint32_t>(this->parent()->get_address() & 0xFFFFFFFF);
  this->pref_ = global_preferences->make_preference<uint32_t>(key);
  if (!this->pref_.load(&this->last_synced_index_))
    this->last_synced_index_ = 0;
}

void FlowerPower::dump_config() {
  ESP_LOGCONFIG(TAG, "Parrot Flower Power");
  ESP_LOGCONFIG(TAG, "  MAC: %s", this->parent()->address_str());
  ESP_LOGCONFIG(TAG, "  Historique: %s (dernier index synchronisé: %u)",
                this->fetch_history_ ? "oui" : "non", this->last_synced_index_);
  LOG_SENSOR("  ", "Air", this->air_temperature_);
  LOG_SENSOR("  ", "Sol", this->soil_temperature_);
  LOG_SENSOR("  ", "Humidité", this->soil_moisture_);
  LOG_SENSOR("  ", "Lumière", this->sunlight_);
  LOG_SENSOR("  ", "EC", this->soil_ec_);
  LOG_SENSOR("  ", "Batterie", this->battery_);
}

void FlowerPower::update() { this->sync_now(); }

void FlowerPower::sync_now() {
  if (this->busy_) {
    ESP_LOGD(TAG, "Session déjà en cours");
    return;
  }
  this->busy_ = true;
  this->read_step_ = 0;
  this->notify_ready_ = 0;
  this->file_.clear();
  this->file_length_ = 0;
  this->frames_in_group_ = 0;
  this->nack_count_ = 0;
  this->group_.clear();
  this->last_activity_ = millis();
  ESP_LOGI(TAG, "Connexion à %s", this->parent()->address_str());
  this->parent()->connect();
}

void FlowerPower::loop() {
  if (!this->publish_q_.empty() && this->history_sample_ != nullptr) {
    this->history_sample_->publish_state(this->publish_q_.front());
    this->publish_q_.pop_front();
  }
  if (this->busy_ && this->last_activity_ != 0 && millis() - this->last_activity_ > 20000) {
    ESP_LOGW(TAG, "Timeout session, déconnexion");
    this->finish_(false);
  }
}

void FlowerPower::set_led(bool on) {
  this->led_pending_ = true;
  this->led_value_ = on;
  if (this->parent()->connected() && this->h_led_ != 0)
    this->write_u8_(this->h_led_, on ? 1 : 0);
  else if (!this->busy_)
    this->sync_now();
}

void FlowerPower::water_now(uint8_t seconds) {
  this->water_pending_ = true;
  this->water_seconds_ = seconds == 0 ? 1 : seconds;
  if (this->h_water_trigger_ == 0 && !this->busy_) {
    this->sync_now();
    return;
  }
  if (this->parent()->connected() && this->h_water_trigger_ != 0)
    this->write_u8_(this->h_water_trigger_, this->water_seconds_);
  else if (!this->busy_)
    this->sync_now();
}

void FlowerPower::publish_(sensor::Sensor *s, float v) {
  if (s != nullptr && std::isfinite(v))
    s->publish_state(v);
}

float FlowerPower::raw_temperature_(uint16_t raw) {
  float t = 0.00000003044f * powf(raw, 3) - 0.00008038f * powf(raw, 2) + raw * 0.1149f - 30.45f;
  return std::min(55.0f, std::max(-10.0f, t));
}
float FlowerPower::raw_moisture_(uint16_t raw) {
  float sm = 11.4293f + (0.0000000010698f * powf(raw, 4) - 0.00000152538f * powf(raw, 3) +
                         0.000866976f * powf(raw, 2) - 0.169422f * raw);
  sm = 100.0f * (0.0000045f * powf(sm, 3) - 0.00055f * powf(sm, 2) + 0.0292f * sm - 0.053f);
  return std::min(60.0f, std::max(0.0f, sm));
}
float FlowerPower::raw_sunlight_(uint16_t raw) {
  if (raw == 0)
    return NAN;
  return 0.0864f * (192773.17f * powf(raw, -1.0606619f));
}
float FlowerPower::hist_temperature_(uint16_t raw) { return 0.0473711045f * raw - 11.19891627f; }
float FlowerPower::hist_moisture_(uint16_t raw) {
  return std::min(100.0f, std::max(0.0f, 0.179814297f * raw - 40.76741498f));
}
float FlowerPower::hist_light_(uint16_t raw) {
  if (raw == 0)
    return NAN;
  return 10981.31391f * expf(1.0f / raw) - 10981.3812f;
}
float FlowerPower::hist_battery_(uint16_t raw) {
  return std::min(100.0f, std::max(0.0f, 0.2865304553f * raw - 177.3583506f));
}

void FlowerPower::read_handle_(uint16_t handle) {
  if (handle == 0)
    return;
  this->last_activity_ = millis();
  esp_ble_gattc_read_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), handle,
                          ESP_GATT_AUTH_REQ_NONE);
}
void FlowerPower::write_u8_(uint16_t handle, uint8_t v) {
  this->last_activity_ = millis();
  esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), handle, 1, &v,
                           ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
}
void FlowerPower::write_u32_le_(uint16_t handle, uint32_t v) {
  uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
  this->last_activity_ = millis();
  esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), handle, 4, b,
                           ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
}

static uint16_t handle_of(ble_client::BLEClient *parent, const char *svc, const char *chr) {
  auto *c = parent->get_characteristic(esp32_ble_tracker::ESPBTUUID::from_raw(svc),
                                      esp32_ble_tracker::ESPBTUUID::from_raw(chr));
  return c == nullptr ? 0 : c->handle;
}
static uint16_t handle_of16(ble_client::BLEClient *parent, uint16_t svc, uint16_t chr) {
  auto *c = parent->get_characteristic(svc, chr);
  return c == nullptr ? 0 : c->handle;
}

void FlowerPower::resolve_handles_() {
  auto *p = this->parent();
  this->h_battery_ = handle_of16(p, 0x180F, 0x2A19);
  this->h_fw_ = handle_of16(p, 0x180A, 0x2A26);
  this->h_air_cal_ = handle_of(p, LIVE_SVC, "39e1fa0a-84a8-11e2-afba-0002a5d5c51b");
  this->h_air_raw_ = handle_of(p, LIVE_SVC, "39e1fa04-84a8-11e2-afba-0002a5d5c51b");
  this->h_soil_raw_ = handle_of(p, LIVE_SVC, "39e1fa03-84a8-11e2-afba-0002a5d5c51b");
  this->h_vwc_cal_ = handle_of(p, LIVE_SVC, "39e1fa09-84a8-11e2-afba-0002a5d5c51b");
  this->h_vwc_raw_ = handle_of(p, LIVE_SVC, "39e1fa05-84a8-11e2-afba-0002a5d5c51b");
  this->h_dli_cal_ = handle_of(p, LIVE_SVC, "39e1fa0b-84a8-11e2-afba-0002a5d5c51b");
  this->h_light_raw_ = handle_of(p, LIVE_SVC, "39e1fa01-84a8-11e2-afba-0002a5d5c51b");
  this->h_ecb_cal_ = handle_of(p, LIVE_SVC, "39e1fa0d-84a8-11e2-afba-0002a5d5c51b");
  this->h_ec_raw_ = handle_of(p, LIVE_SVC, "39e1fa02-84a8-11e2-afba-0002a5d5c51b");
  this->h_led_ = handle_of(p, LIVE_SVC, "39e1fa07-84a8-11e2-afba-0002a5d5c51b");
  this->h_clock_ = handle_of(p, CLOCK_SVC, "39e1fd01-84a8-11e2-afba-0002a5d5c51b");
  this->h_nb_ = handle_of(p, HISTORY_SVC, "39e1fc01-84a8-11e2-afba-0002a5d5c51b");
  this->h_last_ = handle_of(p, HISTORY_SVC, "39e1fc02-84a8-11e2-afba-0002a5d5c51b");
  this->h_session_ = handle_of(p, HISTORY_SVC, "39e1fc04-84a8-11e2-afba-0002a5d5c51b");
  this->h_period_ = handle_of(p, HISTORY_SVC, "39e1fc06-84a8-11e2-afba-0002a5d5c51b");
  this->h_start_ = handle_of(p, HISTORY_SVC, "39e1fc03-84a8-11e2-afba-0002a5d5c51b");
  this->h_tx_ = handle_of(p, UPLOAD_SVC, "39e1fb01-84a8-11e2-afba-0002a5d5c51b");
  this->h_tx_status_ = handle_of(p, UPLOAD_SVC, "39e1fb02-84a8-11e2-afba-0002a5d5c51b");
  this->h_rx_ = handle_of(p, UPLOAD_SVC, "39e1fb03-84a8-11e2-afba-0002a5d5c51b");
  this->h_water_level_ = handle_of(p, WATER_SVC, "39e1f907-84a8-11e2-afba-0002a5d5c51b");
  this->h_water_trigger_ = handle_of(p, WATER_SVC, "39e1f906-84a8-11e2-afba-0002a5d5c51b");
  this->h_water_status_ = handle_of(p, WATER_SVC, "39e1f912-84a8-11e2-afba-0002a5d5c51b");
  this->h_water_mode_ = handle_of(p, WATER_SVC, "39e1f90d-84a8-11e2-afba-0002a5d5c51b");
  ESP_LOGD(TAG, "Handles live air=%u vwc=%u clock=%u hist nb=%u tx=%u eau=%u", this->h_air_cal_, this->h_vwc_cal_,
           this->h_clock_, this->h_nb_, this->h_tx_, this->h_water_level_);
}

void FlowerPower::kick_reads_() {
  const uint16_t order[] = {this->h_battery_, this->h_fw_,     this->h_air_cal_, this->h_air_raw_,
                            this->h_soil_raw_, this->h_vwc_cal_, this->h_vwc_raw_, this->h_dli_cal_,
                            this->h_light_raw_, this->h_ecb_cal_, this->h_ec_raw_, this->h_clock_,
                            this->h_nb_,       this->h_last_,    this->h_session_, this->h_period_,
                            this->h_water_level_, this->h_water_status_, this->h_water_mode_};
  while (this->read_step_ < sizeof(order) / sizeof(order[0]) && order[this->read_step_] == 0)
    this->read_step_++;
  if (this->read_step_ >= sizeof(order) / sizeof(order[0])) {
    if (this->led_pending_ && this->h_led_ != 0)
      this->write_u8_(this->h_led_, this->led_value_ ? 1 : 0);
    if (this->water_pending_ && this->h_water_trigger_ != 0)
      this->write_u8_(this->h_water_trigger_, this->water_seconds_ == 0 ? 1 : this->water_seconds_);
    if (this->fetch_history_)
      this->start_history_();
    else
      this->finish_(true);
    return;
  }
  this->read_handle_(order[this->read_step_]);
}

void FlowerPower::on_read_(uint16_t handle, const uint8_t *data, uint16_t len) {
  this->last_activity_ = millis();
  if (handle == this->h_battery_ && len >= 1)
    this->publish_(this->battery_, data[0]);
  else if (handle == this->h_fw_ && this->firmware_ != nullptr && len > 0)
    this->firmware_->publish_state(std::string(reinterpret_cast<const char *>(data), len));
  else if (handle == this->h_air_cal_ && len >= 4)
    this->publish_(this->air_temperature_, f32le(data));
  else if (handle == this->h_air_raw_ && len >= 2 && this->h_air_cal_ == 0)
    this->publish_(this->air_temperature_, raw_temperature_(le16(data)));
  else if (handle == this->h_soil_raw_ && len >= 2)
    this->publish_(this->soil_temperature_, raw_temperature_(le16(data)));
  else if (handle == this->h_vwc_cal_ && len >= 4)
    this->publish_(this->soil_moisture_, f32le(data));
  else if (handle == this->h_vwc_raw_ && len >= 2 && this->h_vwc_cal_ == 0)
    this->publish_(this->soil_moisture_, raw_moisture_(le16(data)));
  else if (handle == this->h_dli_cal_ && len >= 4)
    this->publish_(this->sunlight_, f32le(data));
  else if (handle == this->h_light_raw_ && len >= 2 && this->h_dli_cal_ == 0)
    this->publish_(this->sunlight_, raw_sunlight_(le16(data)));
  else if (handle == this->h_ecb_cal_ && len >= 4)
    this->publish_(this->soil_ec_, f32le(data));
  else if (handle == this->h_ec_raw_ && len >= 2 && this->h_ecb_cal_ == 0)
    this->publish_(this->soil_ec_, le16(data));
  else if (handle == this->h_clock_ && len >= 4)
    this->clock_s_ = le32(data);
  else if (handle == this->h_nb_ && len >= 2)
    this->nb_entries_ = le16(data);
  else if (handle == this->h_last_ && len >= 4)
    this->last_index_ = le32(data);
  else if (handle == this->h_session_ && len >= 2)
    this->session_id_ = le16(data);
  else if (handle == this->h_period_ && len >= 2) {
    this->session_period_ = le16(data);
    this->publish_(this->session_period_, this->session_period_);
  } else if (handle == this->h_water_level_ && len >= 1)
    this->publish_(this->water_level_, data[0]);
  else if (handle == this->h_water_status_ && len >= 1)
    this->publish_(this->watering_status_, data[0]);
  else if (handle == this->h_water_mode_ && len >= 1)
    this->publish_(this->watering_mode_, data[0]);
  this->read_step_++;
  this->kick_reads_();
}

void FlowerPower::start_history_() {
  if (this->h_tx_ == 0 || this->h_rx_ == 0 || this->h_start_ == 0 || this->nb_entries_ == 0) {
    ESP_LOGW(TAG, "Historique indisponible (entries=%u)", this->nb_entries_);
    this->finish_(true);
    return;
  }
  uint32_t first = this->last_index_ - this->nb_entries_ + 1;
  uint32_t start = std::max(first, this->last_synced_index_ + 1);
  if (start > this->last_index_) {
    ESP_LOGI(TAG, "Historique déjà à jour (index %u)", this->last_index_);
    this->publish_(this->history_entries_, 0);
    this->finish_(true);
    return;
  }
  ESP_LOGI(TAG, "Historique: start=%u last=%u session=%u period=%us", start, this->last_index_,
           this->session_id_, this->session_period_);
  this->write_u32_le_(this->h_start_, start);
  this->parent()->register_for_notify(this->h_tx_);
  this->parent()->register_for_notify(this->h_tx_status_);
}

void FlowerPower::ack_(bool ok) {
  this->write_u8_(this->h_rx_, ok ? 2 : 3);
  if (!ok)
    this->nack_count_++;
  this->group_.clear();
  this->frames_in_group_ = 0;
  if (this->nack_count_ >= 3) {
    ESP_LOGW(TAG, "3 NACK, abandon de l'historique");
    this->write_u8_(this->h_rx_, 5);
    this->finish_(false);
  }
}

void FlowerPower::on_notify_(uint16_t handle, const uint8_t *data, uint16_t len) {
  this->last_activity_ = millis();
  if (handle == this->h_tx_status_ && len >= 1) {
    if (data[0] == 2)
      this->ack_(true);
    else if (data[0] == 0 && this->file_length_ > 0 && this->file_.size() >= this->file_length_)
      this->parse_and_queue_();
    return;
  }
  if (handle != this->h_tx_ || len < 2)
    return;
  // Index de trame : little-endian, comme l'implémentation de terrain romi/flower-power.
  // La spec Parrot dit « big endian » mais le tableau est Index L puis Index H.
  uint16_t index = le16(data);
  if (index == 0 && len >= 6 && this->file_length_ == 0) {
    this->file_length_ = le32(data + 2);
    if (this->file_length_ > 65536) {
      ESP_LOGW(TAG, "Fichier historique trop gros (%u), abandon", this->file_length_);
      this->write_u8_(this->h_rx_, 5);
      this->finish_(false);
      return;
    }
    this->file_.reserve(this->file_length_);
  } else if (index > 0 && len > 2) {
    uint16_t n = len - 2;
    if (this->file_.size() + n > this->file_length_ && this->file_length_ > this->file_.size())
      n = this->file_length_ - this->file_.size();
    this->file_.insert(this->file_.end(), data + 2, data + 2 + n);
  }
  this->frames_in_group_++;
  if (this->file_length_ > 0 && this->file_.size() >= this->file_length_)
    this->parse_and_queue_();
}

void FlowerPower::parse_and_queue_() {
  if (this->file_.size() < 16) {
    this->finish_(false);
    return;
  }
  const uint8_t *h = this->file_.data();
  uint16_t nb = be16(h + 2);
  uint32_t last_time = be32(h + 4);
  uint16_t first_i = be16(h + 8);
  uint16_t last_i = be16(h + 10);
  uint16_t period = be16(h + 14);
  if (period == 0)
    period = this->session_period_ == 0 ? 900 : this->session_period_;
  uint32_t now = static_cast<uint32_t>(::time(nullptr));
  if (now < 1700000000)
    now = 0;
  uint32_t startup = now > this->clock_s_ ? now - this->clock_s_ : now;
  uint16_t published = 0;
  uint16_t available = (this->file_.size() - 16) / 12;
  uint16_t count = std::min<uint16_t>(nb == 0 ? available : nb, available);
  for (uint16_t i = 0; i < count; i++) {
    const uint8_t *e = this->file_.data() + 16 + i * 12;
    uint16_t idx = first_i + i;
    if (idx <= this->last_synced_index_)
      continue;
    if (this->history_publish_limit_ != 0 && published >= this->history_publish_limit_)
      break;
    float air = hist_temperature_(be16(e + 0));
    float light = hist_light_(be16(e + 2));
    uint16_t ec = be16(e + 4);
    float soil = hist_temperature_(be16(e + 6));
    float vwc = hist_moisture_(be16(e + 8));
    float bat = hist_battery_(be16(e + 10));
    uint32_t ts = startup + last_time / 1000 - uint32_t(last_i - idx) * period;
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"ts\":%u,\"index\":%u,\"air_c\":%.2f,\"soil_c\":%.2f,\"vwc\":%.2f,\"light\":%.3f,\"ec\":%u,\"battery\":%.0f}",
             ts, idx, air, soil, vwc, light, ec, bat);
    this->publish_q_.push_back(buf);
    published++;
    this->last_synced_index_ = idx;
  }
  this->pref_.save(&this->last_synced_index_);
  global_preferences->sync();
  this->publish_(this->history_entries_, published);
  ESP_LOGI(TAG, "Historique: %u échantillons nouveaux (fichier %u octets)", published, this->file_.size());
  this->finish_(true);
}

void FlowerPower::finish_(bool ok) {
  ESP_LOGI(TAG, "Fin de session (%s)", ok ? "ok" : "échec");
  this->busy_ = false;
  this->led_pending_ = false;
  this->water_pending_ = false;
  if (this->parent()->connected())
    this->parent()->disconnect();
}

void FlowerPower::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                      esp_ble_gattc_cb_param_t *param) {
  if (event == ESP_GATTC_OPEN_EVT) {
    if (param->open.status != ESP_GATT_OK) {
      ESP_LOGW(TAG, "Connexion refusée, status=%d", param->open.status);
      this->busy_ = false;
    }
    return;
  }
  if (event == ESP_GATTC_DISCONNECT_EVT) {
    this->busy_ = false;
    return;
  }
  if (event == ESP_GATTC_SEARCH_CMPL_EVT) {
    this->resolve_handles_();
    this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
    this->read_step_ = 0;
    this->kick_reads_();
    return;
  }
  if (event == ESP_GATTC_READ_CHAR_EVT && param->read.status == ESP_GATT_OK)
    this->on_read_(param->read.handle, param->read.value, param->read.value_len);
  else if (event == ESP_GATTC_READ_CHAR_EVT) {
    this->read_step_++;
    this->kick_reads_();
  }
  if (event == ESP_GATTC_REG_FOR_NOTIFY_EVT) {
    this->notify_ready_++;
    if (this->notify_ready_ >= 2 && this->h_rx_ != 0)
      this->write_u8_(this->h_rx_, 1);
  }
  if (event == ESP_GATTC_NOTIFY_EVT)
    this->on_notify_(param->notify.handle, param->notify.value, param->notify.value_len);
}

}  // namespace flowerpower
}  // namespace esphome
