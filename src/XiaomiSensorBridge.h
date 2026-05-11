#ifndef XIAOMI_SENSOR_BRIDGE_H
#define XIAOMI_SENSOR_BRIDGE_H

#include <Arduino.h>
#include <Wire.h>
#include "SHTC3_Simulator.h"
#include "data_structures.h"
#include "pins_config.h"

class XiaomiSensorBridge {
public:
    XiaomiSensorBridge();
    ~XiaomiSensorBridge();

    bool begin();
    void update(const System_Global_State& state);
    void setConfig(Configuration* config);

private:
    SHTC3_Simulator shtc3_;
    TwoWire slaveWire_;
    Configuration* config_;
    bool initialized_;  // 标记是否已成功初始化

    float lastBatteryTemp_;
    float lastBoardTemp_;
    float lastSOC_;
    uint32_t lastUpdateTime_;  // 上次更新时间戳（毫秒）

    void updateSOCOutput(float soc);
    static uint8_t socToDAC(float soc);
};

#endif