#ifndef I18N_STRINGS_EN_H
#define I18N_STRINGS_EN_H

#include "i18n.h"

// =============================================================================
// English String Table
// =============================================================================

// Common
static const char s_en_0[] PROGMEM = "Enabled";
static const char s_en_1[] PROGMEM = "Disabled";

// addTip time format
static const char s_en_2[] PROGMEM = "[%02d/%02d %02d:%02d]";

// addTip messages: state transitions
static const char s_en_3[] PROGMEM = "BMS offline, entering CRITICAL";
static const char s_en_4[] PROGMEM = "BQ24780S offline, entering CRITICAL";
static const char s_en_5[] PROGMEM = "BMS fault(%d), entering CRITICAL";
static const char s_en_6[] PROGMEM = "System entering CRITICAL";
static const char s_en_7[] PROGMEM = "Power fault(%d), entering WARNING";
static const char s_en_8[] PROGMEM = "Over-current triggered, entering WARNING";
static const char s_en_9[] PROGMEM = "Over-temp triggered, entering WARNING";
static const char s_en_10[] PROGMEM = "BQ24780S register mismatch, entering WARNING";
static const char s_en_11[] PROGMEM = "BQ76920 register mismatch, entering WARNING";
static const char s_en_12[] PROGMEM = "System entering WARNING";

// addTip messages: BMS faults
static const char s_en_13[] PROGMEM = "BMS over-temp: disable charge/discharge";
static const char s_en_14[] PROGMEM = "BMS over-voltage: disable charge";
static const char s_en_15[] PROGMEM = "BMS under-voltage: disable discharge";
static const char s_en_16[] PROGMEM = "BMS over-current: disable charge/discharge";
static const char s_en_17[] PROGMEM = "BMS short-circuit: emergency shutdown";
static const char s_en_18[] PROGMEM = "BMS chip error";
static const char s_en_19[] PROGMEM = "BMS passive shutdown: disable charge/discharge";
static const char s_en_20[] PROGMEM = "BMS unknown fault(%d)";

// addTip messages: register mismatch
static const char s_en_21[] PROGMEM = "mismatch: reg=%dmA, cfg=%dmA";
static const char s_en_22[] PROGMEM = "mismatch: reg=%dmV, cfg=%dmV";

// addTip messages: battery operations
static const char s_en_23[] PROGMEM = "Battery data reset (SOH/cycles/balancing)";

// addTip messages: charge/discharge events
static const char s_en_24[] PROGMEM = "Charging, SOC:%.1f%%, current:%dmA, voltage:%dmV";
static const char s_en_25[] PROGMEM = "Charge stopped, SOC:%.1f%%";
static const char s_en_26[] PROGMEM = "Balancing started, SOC:%.1f%%, cells: %s";
static const char s_en_27[] PROGMEM = "Balancing stopped, SOC:%.1f%%";

// MQTT entity names
static const char s_en_28[] PROGMEM = "Battery SOC";
static const char s_en_29[] PROGMEM = "Battery SOH";
static const char s_en_30[] PROGMEM = "Battery Voltage";
static const char s_en_31[] PROGMEM = "Battery Current";
static const char s_en_32[] PROGMEM = "Battery Temperature";
static const char s_en_33[] PROGMEM = "Battery Capacity Remaining";
static const char s_en_34[] PROGMEM = "Battery Cycle Count";
static const char s_en_35[] PROGMEM = "Self Consumption Current";
static const char s_en_36[] PROGMEM = "Cell %d Voltage";
static const char s_en_37[] PROGMEM = "Cell Min Voltage";
static const char s_en_38[] PROGMEM = "Cell Max Voltage";
static const char s_en_39[] PROGMEM = "Cell Avg Voltage";
static const char s_en_40[] PROGMEM = "Input Voltage";
static const char s_en_41[] PROGMEM = "Input Current";
static const char s_en_42[] PROGMEM = "Output Power";
static const char s_en_43[] PROGMEM = "Battery Voltage(adc)";
static const char s_en_44[] PROGMEM = "Battery Current(adc)";
static const char s_en_45[] PROGMEM = "AC Power";
static const char s_en_46[] PROGMEM = "Charger Enabled";
static const char s_en_47[] PROGMEM = "Balancing Active";
static const char s_en_48[] PROGMEM = "WiFi Connected";
static const char s_en_49[] PROGMEM = "BMS Fault";
static const char s_en_50[] PROGMEM = "Power Fault";
static const char s_en_51[] PROGMEM = "Emergency Shutdown";
static const char s_en_52[] PROGMEM = "Over Current Protection";
static const char s_en_53[] PROGMEM = "Over Temperature Protection";
static const char s_en_54[] PROGMEM = "Short Circuit Protection";
static const char s_en_55[] PROGMEM = "System Uptime";
static const char s_en_56[] PROGMEM = "Board Temperature";
static const char s_en_57[] PROGMEM = "Environment Temperature";
static const char s_en_58[] PROGMEM = "SHTC3 Temperature";
static const char s_en_59[] PROGMEM = "SHTC3 Humidity";
static const char s_en_60[] PROGMEM = "WiFi RSSI";
static const char s_en_61[] PROGMEM = "Firmware Version";
static const char s_en_62[] PROGMEM = "Power Mode";
static const char s_en_63[] PROGMEM = "Overall Status";
static const char s_en_64[] PROGMEM = "WiFi SSID";
static const char s_en_65[] PROGMEM = "LED Brightness";
static const char s_en_66[] PROGMEM = "Buzzer Volume";
static const char s_en_67[] PROGMEM = "HID Report Mode";
static const char s_en_68[] PROGMEM = "Cell %d Internal Resistance";
static const char s_en_69[] PROGMEM = "IR Sample Count";

// Webhook: API responses
static const char s_en_70[] PROGMEM = "Webhook module not initialized";
static const char s_en_71[] PROGMEM = "Missing request body";
static const char s_en_72[] PROGMEM = "JSON parse failed";
static const char s_en_73[] PROGMEM = "Invalid endpoint index";
static const char s_en_74[] PROGMEM = "Config validation failed";
static const char s_en_75[] PROGMEM = "Config save failed";
static const char s_en_76[] PROGMEM = "Webhook config saved";

// Webhook: test send
static const char s_en_77[] PROGMEM = "WiFi not connected";
static const char s_en_78[] PROGMEM = "Endpoint disabled";
static const char s_en_79[] PROGMEM = "Webhook test";
static const char s_en_80[] PROGMEM = "This is a Webhook test message from ESP32-S3 UPS";

// Webhook: trigger message templates
static const char s_en_81[] PROGMEM = "%s current %.1f exceeds threshold %.1f";
static const char s_en_82[] PROGMEM = "%s current %.1f below threshold %.1f";
static const char s_en_83[] PROGMEM = "%s recovered, current %.1f";
static const char s_en_84[] PROGMEM = "%s recovered: %s";

// Webhook: state strings
static const char s_en_85[] PROGMEM = "AC connected";
static const char s_en_86[] PROGMEM = "AC disconnected";
static const char s_en_87[] PROGMEM = "Charging on";
static const char s_en_88[] PROGMEM = "Charging off";
static const char s_en_89[] PROGMEM = "BMS fault";
static const char s_en_90[] PROGMEM = "BMS normal";
static const char s_en_91[] PROGMEM = "Power fault";
static const char s_en_92[] PROGMEM = "Power normal";
static const char s_en_93[] PROGMEM = "Init";
static const char s_en_94[] PROGMEM = "Normal";
static const char s_en_95[] PROGMEM = "Warning";
static const char s_en_96[] PROGMEM = "Critical";
static const char s_en_97[] PROGMEM = "AC power";
static const char s_en_98[] PROGMEM = "Battery power";
static const char s_en_99[] PROGMEM = "Hybrid power";
static const char s_en_100[] PROGMEM = "Charging";
static const char s_en_101[] PROGMEM = "Emergency shutdown";
static const char s_en_102[] PROGMEM = "Balancing";
static const char s_en_103[] PROGMEM = "Balancing stopped";
static const char s_en_104[] PROGMEM = "Unknown";

// Webhook: template variables
static const char s_en_105[] PROGMEM = "connected";
static const char s_en_106[] PROGMEM = "disconnected";

// Webhook: validation failure reasons
static const char s_en_107[] PROGMEM = "Config version mismatch";
static const char s_en_108[] PROGMEM = "Too many endpoints";
static const char s_en_109[] PROGMEM = "Endpoint %u URL cannot be empty";
static const char s_en_110[] PROGMEM = "Endpoint %u invalid method";
static const char s_en_111[] PROGMEM = "Endpoint %u cooldown cannot be less than 10s";
static const char s_en_112[] PROGMEM = "Endpoint %u too many triggers";
static const char s_en_113[] PROGMEM = "Endpoint %u invalid trigger type";
static const char s_en_114[] PROGMEM = "Endpoint %u invalid compare op";
static const char s_en_115[] PROGMEM = "Endpoint %u value trigger only supports GT/LT";
static const char s_en_116[] PROGMEM = "Endpoint %u state trigger only supports EQ/CHANGE";
static const char s_en_117[] PROGMEM = "Endpoint %u invalid value source";
static const char s_en_118[] PROGMEM = "Endpoint %u invalid state source";
static const char s_en_119[] PROGMEM = "Endpoint %u invalid alert level";

// Pointer array (ordered by StrId)
const char* const I18n::strings_en_[] = {
    s_en_0,  s_en_1,  s_en_2,  s_en_3,  s_en_4,  s_en_5,  s_en_6,  s_en_7,
    s_en_8,  s_en_9,  s_en_10, s_en_11, s_en_12, s_en_13, s_en_14, s_en_15,
    s_en_16, s_en_17, s_en_18, s_en_19, s_en_20, s_en_21, s_en_22, s_en_23,
    s_en_24, s_en_25, s_en_26, s_en_27, s_en_28, s_en_29, s_en_30, s_en_31,
    s_en_32, s_en_33, s_en_34, s_en_35, s_en_36, s_en_37, s_en_38, s_en_39,
    s_en_40, s_en_41, s_en_42, s_en_43, s_en_44, s_en_45, s_en_46, s_en_47,
    s_en_48, s_en_49, s_en_50, s_en_51, s_en_52, s_en_53, s_en_54, s_en_55,
    s_en_56, s_en_57, s_en_58, s_en_59, s_en_60, s_en_61, s_en_62, s_en_63,
    s_en_64, s_en_65, s_en_66, s_en_67, s_en_68, s_en_69,
    s_en_70, s_en_71, s_en_72, s_en_73, s_en_74, s_en_75, s_en_76, s_en_77,
    s_en_78, s_en_79, s_en_80, s_en_81, s_en_82, s_en_83, s_en_84, s_en_85,
    s_en_86, s_en_87, s_en_88, s_en_89, s_en_90, s_en_91, s_en_92, s_en_93,
    s_en_94, s_en_95, s_en_96, s_en_97, s_en_98, s_en_99, s_en_100, s_en_101,
    s_en_102, s_en_103, s_en_104, s_en_105, s_en_106, s_en_107, s_en_108, s_en_109,
    s_en_110, s_en_111, s_en_112, s_en_113, s_en_114, s_en_115, s_en_116, s_en_117,
    s_en_118, s_en_119,
};

#endif
