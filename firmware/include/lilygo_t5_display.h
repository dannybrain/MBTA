#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include <Wire.h>

#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

using lgfx::epd_mode_t;

namespace lilygo_t5 {

constexpr int kPanelWidth = 960;
constexpr int kPanelHeight = 540;
constexpr int kEpdBusSpeedHz = 20000000;
constexpr int kVcomMillivolts = 1560;
constexpr int kI2cSda = 39;
constexpr int kI2cScl = 40;
constexpr uint8_t kPca9535Address = 0x20;
constexpr uint8_t kTps651851Address = 0x68;
constexpr uint8_t kPanelOffsetRotation = 3;
constexpr int kPowerGoodTimeoutMs = 400;

inline uint8_t g_pca_output[2] = {0xFF, 0x00};

inline bool i2cWriteBytes(uint8_t address, const uint8_t *data, size_t length) {
    Wire.beginTransmission(address);
    const size_t written = Wire.write(data, length);
    return written == length && Wire.endTransmission() == 0;
}

inline bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
    const uint8_t buffer[2] = {reg, value};
    return i2cWriteBytes(address, buffer, sizeof(buffer));
}

inline bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t *buffer, size_t length) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(address), static_cast<int>(length)) != length) return false;
    for (size_t i = 0; i < length; ++i) buffer[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

inline bool pca9535Init() {
    g_pca_output[0] = 0xFF;
    g_pca_output[1] = 0x00;
    return i2cWriteRegister(kPca9535Address, 0x02, g_pca_output[0]) &&
           i2cWriteRegister(kPca9535Address, 0x03, g_pca_output[1]) &&
           i2cWriteRegister(kPca9535Address, 0x06, 0x00) &&
           i2cWriteRegister(kPca9535Address, 0x07, 0xC4);
}

inline bool pca9535SetLevel(uint8_t pin, bool level) {
    const uint8_t port = pin / 8;
    const uint8_t bit = pin & 0x07;
    if (level) g_pca_output[port] |= static_cast<uint8_t>(1U << bit);
    else g_pca_output[port] &= static_cast<uint8_t>(~(1U << bit));
    return i2cWriteRegister(kPca9535Address, 0x02 + port, g_pca_output[port]);
}

inline bool pca9535GetLevel(uint8_t pin, bool &level) {
    uint8_t value = 0;
    if (!i2cReadRegister(kPca9535Address, pin / 8, &value, 1)) return false;
    level = (value & (1U << (pin & 0x07))) != 0;
    return true;
}

inline bool tpsWriteRegister(uint8_t reg, const uint8_t *data, size_t length) {
    uint8_t buffer[4] = {reg, 0, 0, 0};
    if (length > 3) return false;
    for (size_t i = 0; i < length; ++i) buffer[i + 1] = data[i];
    return i2cWriteBytes(kTps651851Address, buffer, length + 1);
}

inline bool tpsReadRegisterU8(uint8_t reg, uint8_t &value) {
    return i2cReadRegister(kTps651851Address, reg, &value, 1);
}

class LilyGoT5EpdBus : public lgfx::Bus_EPD {
public:
    bool init() override {
        Wire.begin(kI2cSda, kI2cScl);
        Wire.setClock(400000);
        pinMode(GPIO_NUM_11, OUTPUT);
        digitalWrite(GPIO_NUM_11, LOW);
        pinMode(GPIO_NUM_9, OUTPUT);
        digitalWrite(GPIO_NUM_9, HIGH);
        if (!pca9535Init()) return false;
        return lgfx::Bus_EPD::init();
    }

    bool powerControl(bool power_on) override {
        if (_pwr_on == power_on) return true;
        wait();
        const bool ok = power_on ? powerOnSequence() : powerOffSequence();
        if (ok) _pwr_on = power_on;
        return ok;
    }

private:
    bool waitPcaPowerGood() {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            bool level = false;
            if (!pca9535GetLevel(14, level)) return false;
            if (level) return true;
            delay(1);
        }
        return false;
    }

    bool waitTpsPowerGood() {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            uint8_t value = 0;
            if (!tpsReadRegisterU8(0x0F, value)) return false;
            if ((value & 0xFA) == 0xFA) return true;
            delay(1);
        }
        return false;
    }

    bool powerOnSequence() {
        if (!pca9535SetLevel(8, true)) return false;
        if (!pca9535SetLevel(9, true)) return false;
        if (!pca9535SetLevel(13, true)) return false;
        if (!pca9535SetLevel(11, true)) return false;
        if (!pca9535SetLevel(12, true)) return false;
        delay(1);
        if (!waitPcaPowerGood()) return false;
        if (!tpsWriteRegister(0x01, (const uint8_t[]){0x3F}, 1)) return false;
        const uint16_t vcom = static_cast<uint16_t>(kVcomMillivolts / 10);
        const uint8_t vcom_data[2] = {static_cast<uint8_t>(vcom & 0xFF),
                                      static_cast<uint8_t>((vcom >> 8) & 0xFF)};
        if (!tpsWriteRegister(0x03, vcom_data, sizeof(vcom_data))) return false;
        return waitTpsPowerGood();
    }

    bool powerOffSequence() {
        if (!pca9535SetLevel(8, false)) return false;
        if (!pca9535SetLevel(9, false)) return false;
        if (!pca9535SetLevel(11, false)) return false;
        if (!pca9535SetLevel(12, false)) return false;
        delay(1);
        return pca9535SetLevel(13, false);
    }
};

class LilyGoT5Display : public lgfx::LGFX_Device {
public:
    LilyGoT5Display() {
        auto bus_cfg = bus_.config();
        bus_cfg.bus_speed = kEpdBusSpeedHz;
        bus_cfg.pin_data[0] = GPIO_NUM_5;
        bus_cfg.pin_data[1] = GPIO_NUM_6;
        bus_cfg.pin_data[2] = GPIO_NUM_7;
        bus_cfg.pin_data[3] = GPIO_NUM_15;
        bus_cfg.pin_data[4] = GPIO_NUM_16;
        bus_cfg.pin_data[5] = GPIO_NUM_17;
        bus_cfg.pin_data[6] = GPIO_NUM_18;
        bus_cfg.pin_data[7] = GPIO_NUM_8;
        bus_cfg.pin_pwr = GPIO_NUM_1;
        bus_cfg.pin_spv = GPIO_NUM_45;
        bus_cfg.pin_ckv = GPIO_NUM_48;
        bus_cfg.pin_sph = GPIO_NUM_41;
        bus_cfg.pin_oe = GPIO_NUM_1;
        bus_cfg.pin_le = GPIO_NUM_42;
        bus_cfg.pin_cl = GPIO_NUM_4;
        bus_cfg.bus_width = 8;
        bus_.config(bus_cfg);
        panel_.setBus(&bus_);
        auto panel_cfg = panel_.config();
        panel_cfg.memory_width = kPanelWidth;
        panel_cfg.memory_height = kPanelHeight;
        panel_cfg.panel_width = kPanelWidth;
        panel_cfg.panel_height = kPanelHeight;
        panel_cfg.offset_rotation = kPanelOffsetRotation;
        panel_cfg.bus_shared = false;
        panel_.config(panel_cfg);
        auto detail = panel_.config_detail();
        detail.line_padding = 0;
        detail.task_priority = 3;
        panel_.config_detail(detail);
        setPanel(&panel_);
    }

private:
    LilyGoT5EpdBus bus_;
    lgfx::Panel_EPD panel_;
};

inline void flushDisplay(LilyGoT5Display &display) {
    display.setAutoDisplay(false);
    display.setColorDepth(4);
    display.setRotation(0);
    display.setEpdMode(epd_mode_t::epd_quality);
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
}

}  // namespace lilygo_t5
