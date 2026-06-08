#include "shtc3.h"
#include <Arduino.h>
#include "utils.h"
#include "debug.h"

SHTC3::SHTC3(I2CInterface& i2c)
    : i2c_(&i2c), available_(false),
      state_(SHTC3_IDLE), measure_start_(0), last_read_(0),
      temperature_(0), humidity_(0) {}

bool SHTC3::begin() {
    // SHTC3 休眠模式下不响应 I2C 地址探测，所以先发唤醒命令再检测
    // 即使传感器不在，发命令也不会造成问题
    sendCommand(SHTC3_CMD_WAKEUP);
    delayMicroseconds(340);  // 等待唤醒完成

    if (!i2c_->isDeviceConnected(SHTC3_ADDRESS)) {
        DBG.println(F("SHTC3: not found at 0x70"));
        available_ = false;
        return false;
    }
    DBG.println(F("SHTC3: found at 0x70"));

    // 软复位，确保传感器处于已知状态
    if (!sendCommand(SHTC3_CMD_SOFT_RESET)) {
        DBG.println(F("SHTC3: soft reset failed"));
        available_ = false;
        return false;
    }
    delayMicroseconds(340);  // 复位后等待 ≥240µs，留余量

    // 再次唤醒，准备进入工作状态
    if (!sendCommand(SHTC3_CMD_WAKEUP)) {
        DBG.println(F("SHTC3: wake failed"));
        available_ = false;
        return false;
    }
    // 不休眠，让传感器保持就绪状态

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
                if (!sendCommand(SHTC3_CMD_WAKEUP)) {
                    DBG.println(F("SHTC3: wakeup cmd fail"));
                    last_read_ = now;  // 避免连续重试
                    break;
                }
                delay(2);  // 等待唤醒完成
                if (!sendCommand(SHTC3_CMD_READ_TF)) {
                    DBG.println(F("SHTC3: read cmd fail"));
                    last_read_ = now;
                    break;
                }
                DBG.println(F("SHTC3: measuring..."));
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
                    DBG.printf("SHTC3: T=%.1f H=%.1f\n", t, h);
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
    if (!i2c_->readRaw(SHTC3_ADDRESS, buf, 6)) {
        DBG.println(F("SHTC3: readRaw fail"));
        return false;
    }

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
