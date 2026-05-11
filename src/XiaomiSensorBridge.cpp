#include "XiaomiSensorBridge.h"

XiaomiSensorBridge::XiaomiSensorBridge()
    : slaveWire_(1), config_(nullptr), initialized_(false), lastBatteryTemp_(25.0f), lastBoardTemp_(25.0f), lastSOC_(50.0f), lastUpdateTime_(0) {
}

XiaomiSensorBridge::~XiaomiSensorBridge() {
    // 1. 清除初始化标志，防止 update 在销毁过程中被调用
    initialized_ = false;
    
    // 2. 停止 PWM 输出并恢复引脚为输入
    analogWrite(XIAOMI_POWER_PIN, 0);
    pinMode(XIAOMI_POWER_PIN, INPUT);
    
    // 3. 清理 I2C 从机回调（防止悬空指针）
    shtc3_.cleanup();
}

void XiaomiSensorBridge::setConfig(Configuration* config) {
    config_ = config;
}

bool XiaomiSensorBridge::begin() {
    Serial.println(F("XiaomiBridge: Initializing..."));

    // 初始化从机I2C, SHTC3_Simulator内部负责调用wire.begin()
    shtc3_.begin(slaveWire_, XIAOMI_SDA_PIN, XIAOMI_SCL_PIN);

    // 初始化GPIO10为PWM输出
    pinMode(XIAOMI_POWER_PIN, OUTPUT);
    analogWrite(XIAOMI_POWER_PIN, 0);

    initialized_ = true;  // 标记为已初始化
    Serial.println(F("XiaomiBridge: Init complete"));
    return true;
}

void XiaomiSensorBridge::update(const System_Global_State& state) {
    // 检查是否已成功初始化
    if (!initialized_) return;

    // 固定 3 秒更新间隔，不依赖数据变化阈值
    uint32_t currentTime = millis();
    if (currentTime - lastUpdateTime_ < 3000) {
        return;  // 未到更新时间，直接返回
    }

    // 温度 → 电池温度 (来自BMS)
    // 湿度 → 板载NTC温度 (编码为"湿度"供米家显示)
    float batteryTemp = state.bms.temperature;
    float boardTemp = state.system.board_temperature;
    float soc = state.bms.soc;

    // 无条件更新 I2C 传感器数据
    shtc3_.setValues(batteryTemp, boardTemp);
    lastBatteryTemp_ = batteryTemp;
    lastBoardTemp_ = boardTemp;

    // 无条件更新 SOC 输出到 GPIO10
    updateSOCOutput(soc);
    lastSOC_ = soc;

    // 更新最后更新时间戳
    lastUpdateTime_ = currentTime;
}

void XiaomiSensorBridge::updateSOCOutput(float soc) {
    uint8_t dacValue = socToDAC(soc);
    analogWrite(XIAOMI_POWER_PIN, dacValue);
    Serial.printf_P(PSTR("XiaomiBridge: SOC=%.1f%% -> PWM=%u\n"), soc, dacValue);
}

// SOC百分比映射到PWM占空比值
// 电压曲线: 0% ≈ 2.2V, 100% ≈ 3.0V
// ESP32-S3 analogWrite (PWM): 0=0V, 255=3.3V
// 2.2V ≈ 169/255*3.3V, 3.0V ≈ 232/255*3.3V
// 使用幂曲线平滑过渡: pwmValue = 169 + 63 * (soc/100)^1.5
uint8_t XiaomiSensorBridge::socToDAC(float soc) {
    float t = constrain(soc, 0.0f, 100.0f) / 100.0f;
    float curve = powf(t, 1.5f);
    uint8_t pwmValue = (uint8_t)(169.0f + 63.0f * curve);
    return constrain(pwmValue, (uint8_t)169, (uint8_t)232);
}
