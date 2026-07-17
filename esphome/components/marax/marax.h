// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 JC-23
#pragma once

#include "bitmaps.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome {
namespace marax {

static const char *const TAG = "marax";

class MaraXComponent : public Component, public uart::UARTDevice {
 public:
  void set_firmware_sensor(text_sensor::TextSensor *s) { firmware_sensor_ = s; }
  void set_mode_sensor(text_sensor::TextSensor *s) { mode_sensor_ = s; }
  void set_steam_sensor(sensor::Sensor *s) { steam_sensor_ = s; }
  void set_target_steam_sensor(sensor::Sensor *s) { target_steam_sensor_ = s; }
  void set_hx_sensor(sensor::Sensor *s) { hx_sensor_ = s; }
  void set_boost_sensor(sensor::Sensor *s) { boost_sensor_ = s; }
  void set_heating_sensor(binary_sensor::BinarySensor *s) { heating_sensor_ = s; }
  void set_pump_sensor(binary_sensor::BinarySensor *s) { pump_sensor_ = s; }
  void set_shot_count_sensor(sensor::Sensor *s) { shot_count_sensor_ = s; }

  void setup() override {
    last_rx_ms_ = millis();
    shot_count_sensor_->publish_state(0);
    pump_sensor_->publish_state(false);
  }

  void loop() override {
    read_uart_();
    update_shot_timer_();

    const uint32_t now = millis();
    if (now - last_rx_ms_ > 1000) {
      data_available_ = false;
      frame_.clear();
      frame_overflow_ = false;
    }
  }

  void set_reed_state(bool state) {
    reed_state_ = state;
    if (state) {
      pump_off_since_ms_ = 0;
      set_pump_state_(true);
    } else if (pump_state_ && pump_off_since_ms_ == 0) {
      pump_off_since_ms_ = millis();
    }
  }

  bool has_data() const { return data_available_; }
  bool pump_running() const { return pump_state_; }
  bool heating() const { return heating_; }
  int steam_temperature() const { return steam_temperature_; }
  int hx_temperature() const { return hx_temperature_; }
  int shot_seconds() const { return shot_seconds_; }
  char mode() const { return mode_; }

 protected:
  void read_uart_() {
    while (available()) {
      uint8_t byte;
      if (!read_byte(&byte))
        break;
      const char value = static_cast<char>(byte);
      last_rx_ms_ = millis();

      if (value == '\r')
        continue;
      if (value == '\n') {
        if (!frame_overflow_ && !frame_.empty())
          parse_frame_(frame_);
        frame_.clear();
        frame_overflow_ = false;
        continue;
      }
      if (frame_overflow_)
        continue;
      if (frame_.size() < 31)
        frame_.push_back(value);
      else {
        frame_.clear();
        frame_overflow_ = true;
        ESP_LOGW(TAG, "Serial frame too long; discarded");
      }
    }
  }

  void parse_frame_(const std::string &frame) {
    std::string fields[7];
    size_t start = 0;
    int count = 0;
    while (start <= frame.size() && count < 7) {
      const size_t comma = frame.find(',', start);
      fields[count++] = frame.substr(start, comma - start);
      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
    if (count < 6 || count > 7 || fields[0].size() < 2) {
      ESP_LOGW(TAG, "Invalid MaraX V1 frame: %s", frame.c_str());
      return;
    }

    mode_ = fields[0][0];
    firmware_ = fields[0].substr(1);
    steam_temperature_ = std::atoi(fields[1].c_str());
    target_steam_temperature_ = std::atoi(fields[2].c_str());
    hx_temperature_ = std::atoi(fields[3].c_str());
    boost_countdown_ = std::atoi(fields[4].c_str());
    heating_ = std::atoi(fields[5].c_str()) == 1;
    data_available_ = true;

    firmware_sensor_->publish_state(firmware_);
    mode_sensor_->publish_state(std::string(1, mode_));
    steam_sensor_->publish_state(steam_temperature_);
    target_steam_sensor_->publish_state(target_steam_temperature_);
    hx_sensor_->publish_state(hx_temperature_);
    boost_sensor_->publish_state(boost_countdown_);
    heating_sensor_->publish_state(heating_);
  }

  void set_pump_state_(bool state) {
    if (pump_state_ == state)
      return;
    pump_state_ = state;
    pump_sensor_->publish_state(state);

    const uint32_t now = millis();
    if (state) {
      shot_running_ = true;
      shot_seconds_ = 0;
      shot_started_ms_ = now;
      shot_stopped_ms_ = 0;
      ESP_LOGI(TAG, "Pump on");
    } else {
      if (shot_running_ && shot_seconds_ >= 20) {
        ++shot_count_;
        shot_count_sensor_->publish_state(shot_count_);
      }
      shot_running_ = false;
      shot_stopped_ms_ = now;
      ESP_LOGI(TAG, "Pump off after %d seconds", shot_seconds_);
    }
  }

  void update_shot_timer_() {
    const uint32_t now = millis();
    if (!reed_state_ && pump_state_ && pump_off_since_ms_ != 0 &&
        now - pump_off_since_ms_ >= 700) {
      pump_off_since_ms_ = 0;
      set_pump_state_(false);
    }
    if (shot_running_)
      shot_seconds_ = std::min<int>(99, (now - shot_started_ms_) / 1000);
    else if (shot_stopped_ms_ != 0 && now - shot_stopped_ms_ >= 4000) {
      shot_stopped_ms_ = 0;
      shot_seconds_ = 0;
    }
  }

  text_sensor::TextSensor *firmware_sensor_{};
  text_sensor::TextSensor *mode_sensor_{};
  sensor::Sensor *steam_sensor_{};
  sensor::Sensor *target_steam_sensor_{};
  sensor::Sensor *hx_sensor_{};
  sensor::Sensor *boost_sensor_{};
  binary_sensor::BinarySensor *heating_sensor_{};
  binary_sensor::BinarySensor *pump_sensor_{};
  sensor::Sensor *shot_count_sensor_{};

  std::string frame_;
  std::string firmware_;
  bool frame_overflow_{false};
  bool data_available_{false};
  bool reed_state_{false};
  bool pump_state_{false};
  bool shot_running_{false};
  bool heating_{false};
  char mode_{'C'};
  int steam_temperature_{0};
  int target_steam_temperature_{0};
  int hx_temperature_{0};
  int boost_countdown_{0};
  int shot_seconds_{0};
  uint32_t shot_count_{0};
  uint32_t last_rx_ms_{0};
  uint32_t pump_off_since_ms_{0};
  uint32_t shot_started_ms_{0};
  uint32_t shot_stopped_ms_{0};
};

}  // namespace marax
}  // namespace esphome
