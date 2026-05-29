#ifndef SHTC3_H
#define SHTC3_H

#include <stdint.h>
#include "i2c_interface.h"

#define SHTC3_ADDRESS       0x38    // 实际地址 0x70，ESP32 Wire 需右移一位

// 命令定义 (MSB first)
#define SHTC3_CMD_WAKEUP    0x3517
#define SHTC3_CMD_SLEEP     0xB098
#define SHTC3_CMD_READ_TF   0x7866  // Temperature first, no clock stretching

#define SHTC3_READ_INTERVAL_MS  30000   // 读取间隔 30 秒
#define SHTC3_MEASURE_WAIT_MS   15      // 测量等待 > 12.1ms

typedef enum {
    SHTC3_IDLE = 0,
    SHTC3_MEASURING,
} SHTC3_State_t;

class SHTC3 {
public:
    explicit SHTC3(I2CInterface& i2c);

    bool begin();                   // 检测+初始化，不存在返回 false
    void update();                  // 每次循环调用，内部状态机自动运行
    bool isAvailable() const { return available_; }
    float getTemperature() const { return temperature_; }
    float getHumidity() const { return humidity_; }

private:
    I2CInterface* i2c_;
    bool available_;
    SHTC3_State_t state_;
    unsigned long measure_start_;   // 发送测量命令的时间戳
    unsigned long last_read_;       // 上次完成读取的时间戳
    float temperature_;
    float humidity_;

    bool sendCommand(uint16_t cmd);
    bool readData(float& temperature, float& humidity);
};

#endif
