#include "shtc3.h"
#include <Arduino.h>
#include "utils.h"
#include "debug.h"

SHTC3::SHTC3(I2CInterface& i2c)
    : i2c_(&i2c), available_(false),
      state_(SHTC3_IDLE), measure_start_(0), last_read_(0),
      temperature_(0), humidity_(0) {}

bool SHTC3::begin() {
    if (!i2c_->isDeviceConnected(SHTC3_ADDRESS)) {
        DBG.println(F("SHTC3: not found"));
        available_ = false;
        return false;
    }

    // 唤醒并休眠，验证通信
    if (!sendCommand(SHTC3_CMD_WAKEUP)) {
        DBG.println(F("SHTC3: wake failed"));
        available_ = false;
        return false;
    }
    delayMicroseconds(240);
    sendCommand(SHTC3_CMD_SLEEP);

    available_ = true;
    DBG.println(F("SHTC3: init OK"));
    return true;
}

void SHTC3::update() {
    if (!available_) return;

    unsigned long now = millis();

    switch (state_) {
        case SHTC3_IDLE:
            // 30 秒触发一次
            if (now - last_read_ >= SHTC3_READ_INTERVAL_MS) {
                sendCommand(SHTC3_CMD_WAKEUP);
                delayMicroseconds(240);
                sendCommand(SHTC3_CMD_READ_TF);
                measure_start_ = now;
                state_ = SHTC3_MEASURING;
            }
            break;

        case SHTC3_MEASURING:
            // 等待 ≥ 15ms
            if (now - measure_start_ >= SHTC3_MEASURE_WAIT_MS) {
                float t, h;
                if (readData(t, h)) {
                    temperature_ = t;
                    humidity_ = h;
                }
                sendCommand(SHTC3_CMD_SLEEP);
                last_read_ = now;
                state_ = SHTC3_IDLE;
            }
            break;
    }
}

bool SHTC3::sendCommand(uint16_t cmd) {
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_->writeCommand(SHTC3_ADDRESS, buf, 2);
}

bool SHTC3::readData(float& temperature, float& humidity) {
    uint8_t buf[6];
    if (!i2c_->readRaw(SHTC3_ADDRESS, buf, 6)) return false;

    if (Utils::shtc3Crc8(buf, 2) != buf[2]) {
        DBG.println(F("SHTC3: temp CRC fail"));
        return false;
    }
    if (Utils::shtc3Crc8(buf + 3, 2) != buf[5]) {
        DBG.println(F("SHTC3: hum CRC fail"));
        return false;
    }

    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_h = ((uint16_t)buf[3] << 8) | buf[4];

    temperature = -45.0f + 175.0f * (float)raw_t / 65536.0f;
    humidity = 100.0f * (float)raw_h / 65536.0f;

    if (humidity < 0.0f) humidity = 0.0f;
    if (humidity > 100.0f) humidity = 100.0f;

    return true;
}
