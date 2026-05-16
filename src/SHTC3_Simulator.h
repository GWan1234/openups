#ifndef SHTC3_SIMULATOR_H
#define SHTC3_SIMULATOR_H

#include <Arduino.h>
#include <Wire.h>

#define SHTC3_I2C_ADDR  0x70  //or >>1 0x38

class SHTC3_Simulator {
public:
    void begin(TwoWire &wire, int sdaPin, int sclPin);

    void setTemperature(float tempC);
    void setHumidity(float rhPercent);
    void setValues(float tempC, float rhPercent);
    void cleanup();
    void update();

private:
    TwoWire *_wire;
    float _temperature = 20.0f;
    float _humidity = 70.0f;

    uint8_t _txBuf[6];
    uint8_t _txLen = 0;

    // 预计算的两份完整响应（含CRC），温度在前 / 湿度在前
    uint8_t _respTFirst[6];
    uint8_t _respRHFirst[6];

    static SHTC3_Simulator *_instance;

    static void onReceive(int len);
    static void onRequest();

    void prepareResponse(uint16_t cmd);
    void rebuildBuffers();

    static uint8_t crc8(const uint8_t *data, uint8_t len);
    uint16_t toRawTemp(float c);
    uint16_t toRawRH(float rh);
};

#endif
