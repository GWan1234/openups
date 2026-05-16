#include "XiaomiSensorBridge.h"
#include "debug.h"

XiaomiSensorBridge::XiaomiSensorBridge()
    : slaveWire_(1), config_(nullptr), initialized_(false), lastUpdateTime_(0) {
}

XiaomiSensorBridge::~XiaomiSensorBridge() {
    initialized_ = false;
    ledcWrite(XIAOMI_POWER_PIN, 0);
    ledcDetach(XIAOMI_POWER_PIN);
    shtc3_.cleanup();
}

void XiaomiSensorBridge::setConfig(Configuration* config) {
    config_ = config;
}

bool XiaomiSensorBridge::begin() {
    DBG.println(F("XiaomiBridge: Initializing..."));

    shtc3_.setValues(20.0f, 70.0f);
    shtc3_.begin(slaveWire_, XIAOMI_SDA_PIN, XIAOMI_SCL_PIN);

    if (!ledcAttach(XIAOMI_POWER_PIN, 150000, 8)) {
        DBG.println(F("XiaomiBridge: ledcAttach failed"));
        return false;
    }

    //软启动
    for (int i = 0; i <= 232; i += 4) {
        ledcWrite(XIAOMI_POWER_PIN, i);
        delayMicroseconds(20);
    }

    initialized_ = true;
    DBG.println(F("XiaomiBridge: Init complete"));
    return true;
}

void XiaomiSensorBridge::update(const System_Global_State& state) {
    if (!initialized_) return;

    uint32_t currentTime = millis();
    if (currentTime - lastUpdateTime_ < 3000) {
        return;
    }

    float batteryTemp = state.bms.temperature;
    float soh = state.bms.soh;
    float soc = state.bms.soc;

    shtc3_.setValues(batteryTemp, soc);
    updateSOHOutput(soh);

    lastUpdateTime_ = currentTime;
}

void XiaomiSensorBridge::updateSOHOutput(float soh) {
    ledcWrite(XIAOMI_POWER_PIN, sohToDAC(soh));
}

uint8_t XiaomiSensorBridge::sohToDAC(float soh) {
    static const uint8_t table[][2] = {
        {  0, 193}, // 2.50V
        {  5, 202}, // 2.62V
        { 10, 207}, // 2.68V
        { 20, 212}, // 2.74V
        { 30, 215}, // 2.78V
        { 40, 217}, // 2.81V
        { 50, 219}, // 2.83V
        { 60, 220}, // 2.85V
        { 70, 223}, // 2.88V
        { 80, 225}, // 2.91V
        { 90, 228}, // 2.95V
        {100, 232}, // 3.00V
    };
    constexpr int COUNT = sizeof(table) / sizeof(table[0]);

    float v = constrain(soh, 50.0f, 100.0f);
    if (v <= table[0][0]) return table[0][1];
    if (v >= table[COUNT - 1][0]) return table[COUNT - 1][1];

    for (int i = 1; i < COUNT; i++) {
        if (v <= table[i][0]) {
            float x0 = table[i - 1][0], y0 = table[i - 1][1];
            float x1 = table[i][0],     y1 = table[i][1];
            return (uint8_t)(y0 + (y1 - y0) * (v - x0) / (x1 - x0));
        }
    }
    return table[COUNT - 1][1];
}
