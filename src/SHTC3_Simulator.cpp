#include "SHTC3_Simulator.h"

SHTC3_Simulator *SHTC3_Simulator::_instance = nullptr;

void SHTC3_Simulator::begin(TwoWire &wire, int sdaPin, int sclPin) {
    _wire = &wire;
    _instance = this;
    _wire->begin((uint8_t)SHTC3_I2C_ADDR, sdaPin, sclPin, 0);
    _wire->onReceive(SHTC3_Simulator::onReceive);
    _wire->onRequest(SHTC3_Simulator::onRequest);
}

void SHTC3_Simulator::cleanup() {
    // 清除全局实例指针，防止悬空指针访问
    if (_instance == this) {
        _instance = nullptr;
    }
    // 注意：ESP32 Wire 库不支持动态注销回调
    // 但通过将 _instance 置 nullptr，回调函数中的安全检查会阻止访问已释放对象
}

void SHTC3_Simulator::setTemperature(float tempC) {
    _temperature = constrain(tempC, -40.0f, 125.0f);
}

void SHTC3_Simulator::setHumidity(float rhPercent) {
    _humidity = constrain(rhPercent, 0.0f, 100.0f);
}

void SHTC3_Simulator::setValues(float tempC, float rhPercent) {
    setTemperature(tempC);
    setHumidity(rhPercent);
}

void SHTC3_Simulator::update() {
}

void SHTC3_Simulator::onReceive(int len) {
    if (!_instance || len < 2) return;

    _instance->_txLen = 0;

    TwoWire *w = _instance->_wire;
    uint8_t hi = w->read();
    uint8_t lo = w->read();
    while (w->available()) w->read();

    _instance->prepareResponse((uint16_t)hi << 8 | lo);
}

void SHTC3_Simulator::onRequest() {
    if (!_instance) return;
    _instance->_wire->write(_instance->_txBuf, _instance->_txLen);
}

void SHTC3_Simulator::prepareResponse(uint16_t cmd) {
    switch (cmd) {
        case 0x7CA2: case 0x5C24:
        case 0x7866: case 0x58E0:
        case 0x6458: case 0x44DE:
        case 0x609C: case 0x401A: {
            bool tFirst = (cmd == 0x7CA2 || cmd == 0x7866 ||
                           cmd == 0x6458 || cmd == 0x609C);

            uint16_t rawT  = toRawTemp(_temperature);
            uint16_t rawRH = toRawRH(_humidity);

            uint8_t t[2]  = { (uint8_t)(rawT >> 8), (uint8_t)(rawT & 0xFF) };
            uint8_t rh[2] = { (uint8_t)(rawRH >> 8), (uint8_t)(rawRH & 0xFF) };

            if (tFirst) {
                _txBuf[0] = t[0];  _txBuf[1] = t[1];  _txBuf[2] = crc8(t, 2);
                _txBuf[3] = rh[0]; _txBuf[4] = rh[1]; _txBuf[5] = crc8(rh, 2);
            } else {
                _txBuf[0] = rh[0]; _txBuf[1] = rh[1]; _txBuf[2] = crc8(rh, 2);
                _txBuf[3] = t[0];  _txBuf[4] = t[1];  _txBuf[5] = crc8(t, 2);
            }
            _txLen = 6;
            break;
        }

        case 0xEFC8: {
            uint8_t id[2] = { 0x08, 0x07 };
            _txBuf[0] = id[0];
            _txBuf[1] = id[1];
            _txBuf[2] = crc8(id, 2);
            _txLen = 3;
            break;
        }

        case 0x3517:
        case 0xB098:
        case 0x805D:
            _txLen = 0;
            break;

        default:
            _txLen = 0;
            break;
    }
}

uint8_t SHTC3_Simulator::crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

uint16_t SHTC3_Simulator::toRawTemp(float c) {
    return (uint16_t)((c + 45.0f) * 65535.0f / 175.0f);
}

uint16_t SHTC3_Simulator::toRawRH(float rh) {
    return (uint16_t)(rh * 65535.0f / 100.0f);
}
