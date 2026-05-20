#include "XiaomiSensorBridge.h"
#include "debug.h"

XiaomiSensorBridge::XiaomiSensorBridge()
    : slaveWire_(1), config_(nullptr), initialized_(false), lastUpdateTime_(0) {
}

XiaomiSensorBridge::~XiaomiSensorBridge() {
    initialized_ = false;
    digitalWrite(XIAOMI_POWER_PIN, LOW);
    shtc3_.cleanup();
}

void XiaomiSensorBridge::setConfig(Configuration* config) {
    config_ = config;
}

bool XiaomiSensorBridge::begin() {
    DBG.println(F("XiaomiBridge: Initializing..."));

    shtc3_.setValues(20.0f, 70.0f);
    shtc3_.begin(slaveWire_, XIAOMI_SDA_PIN, XIAOMI_SCL_PIN);

    pinMode(XIAOMI_POWER_PIN, OUTPUT);
    digitalWrite(XIAOMI_POWER_PIN, HIGH);

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
    float soc = state.bms.soc;

    shtc3_.setValues(batteryTemp, soc);

    lastUpdateTime_ = currentTime;
}
