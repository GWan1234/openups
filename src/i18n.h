#ifndef I18N_H
#define I18N_H

#include <Arduino.h>

// =============================================================================
// 语言枚举
// =============================================================================
enum Language : uint8_t {
    LANG_ZH = 0,    // 中文（默认）
    LANG_EN = 1,    // 英文
    LANG_COUNT = 2
};

// =============================================================================
// 字符串 ID 枚举 — 服务器端可翻译字符串
// （前端 UI 字符串由 JS 语言包处理，不在此枚举中）
// =============================================================================
enum StrId : uint16_t {
    // === 通用 ===
    STR_ENABLED = 0,                // "已启用" / "Enabled"
    STR_DISABLED,                   // "已禁用" / "Disabled"

    // === addTip 时间格式 ===
    STR_TIP_TIME_FMT,              // "[%d月%d日 %02d:%02d]" / "[%02d/%02d %02d:%02d]"

    // === addTip 消息：状态切换 ===
    STR_TIP_BMS_OFFLINE,            // "BMS模块掉线，进入危急状态"
    STR_TIP_BQ24780S_OFFLINE,       // "电源芯片BQ24780S掉线，进入危急状态"
    STR_TIP_BMS_FAULT,              // "BMS故障(%d)，进入危急状态"
    STR_TIP_SYSTEM_CRITICAL,        // "系统进入危急状态"
    STR_TIP_POWER_FAULT,            // "电源故障(%d)，进入警告状态"
    STR_TIP_OVER_CURRENT,           // "过流保护触发，进入警告状态"
    STR_TIP_OVER_TEMP,              // "过温保护触发，进入警告状态"
    STR_TIP_BQ24780S_REG_MISMATCH,  // "BQ24780S寄存器配置不一致，进入警告状态"
    STR_TIP_BQ76920_REG_MISMATCH,   // "BQ76920寄存器配置不一致，进入警告状态"
    STR_TIP_SYSTEM_WARNING,         // "系统进入警告状态"

    // === addTip 消息：BMS 故障 ===
    STR_TIP_BMS_OVER_TEMP,          // "BMS过温保护：关闭充放电"
    STR_TIP_BMS_OVER_VOLTAGE,       // "BMS过压保护：关闭充电"
    STR_TIP_BMS_UNDER_VOLTAGE,      // "BMS欠压保护：关闭放电"
    STR_TIP_BMS_OVER_CURRENT,       // "BMS过流保护：关闭充放电"
    STR_TIP_BMS_SHORT_CIRCUIT,      // "BMS短路保护：紧急关机"
    STR_TIP_BMS_CHIP_ERROR,         // "BMS芯片错误"
    STR_TIP_BMS_PASSIVE_SHUTDOWN,   // "BMS被动关机：关闭充放电"
    STR_TIP_BMS_UNKNOWN_FAULT,      // "BMS未知故障(%d)"

    // === addTip 消息：寄存器不一致 ===
    STR_TIP_REG_MISMATCH_mA,        // "不一致:寄存器=%dmA,配置=%dmA"
    STR_TIP_REG_MISMATCH_mV,        // "不一致:寄存器=%dmV,配置=%dmV"

    // === addTip 消息：电池操作 ===
    STR_TIP_BATTERY_RESET,          // "电池数据已重置 (SOH/循环/均衡)"

    // === addTip 消息：充放电事件 ===
    STR_TIP_CHARGING,               // "正在充电，SOC:%.1f%%，充电电流：%dmA，充电电压：%dmV"
    STR_TIP_CHARGE_STOPPED,         // "充电已停止，SOC:%.1f%%"
    STR_TIP_BALANCE_START,          // "电池均衡启动，SOC:%.1f%%，均衡电芯：%s"
    STR_TIP_BALANCE_STOP,           // "电池均衡停止，SOC:%.1f%%"

    // === MQTT 实体名称：传感器 ===
    STR_MQTT_BMS_SOC,               // "UPS Battery SOC"
    STR_MQTT_BMS_SOH,               // "UPS Battery SOH"
    STR_MQTT_BMS_VOLTAGE,           // "UPS Battery Voltage"
    STR_MQTT_BMS_CURRENT,           // "UPS Battery Current"
    STR_MQTT_BMS_TEMPERATURE,       // "UPS Battery Temperature"
    STR_MQTT_BMS_CAPACITY,          // "UPS Battery Capacity Remaining"
    STR_MQTT_BMS_CYCLE_COUNT,       // "UPS Battery Cycle Count"
    STR_MQTT_BMS_SELF_CONSUMPTION,  // "UPS Self Consumption Current"
    STR_MQTT_CELL_VOLTAGE,          // "UPS Cell %d Voltage"
    STR_MQTT_CELL_MIN,              // "UPS Cell Min Voltage"
    STR_MQTT_CELL_MAX,              // "UPS Cell Max Voltage"
    STR_MQTT_CELL_AVG,              // "UPS Cell Avg Voltage"
    STR_MQTT_INPUT_VOLTAGE,         // "UPS Input Voltage"
    STR_MQTT_INPUT_CURRENT,         // "UPS Input Current"
    STR_MQTT_OUTPUT_POWER,          // "UPS Output Power"
    STR_MQTT_BATT_VOLTAGE_ADC,      // "UPS Battery Voltage(adc)"
    STR_MQTT_BATT_CURRENT_ADC,      // "UPS Battery Current(adc)"
    STR_MQTT_AC_POWER,              // "UPS AC Power"
    STR_MQTT_CHARGER_ENABLED,       // "UPS Charger Enabled"
    STR_MQTT_BALANCING_ACTIVE,      // "UPS Balancing Active"
    STR_MQTT_WIFI_CONNECTED,        // "UPS WiFi Connected"
    STR_MQTT_BMS_FAULT,             // "UPS BMS Fault"
    STR_MQTT_POWER_FAULT,           // "UPS Power Fault"
    STR_MQTT_EMERGENCY_SHUTDOWN,    // "UPS Emergency Shutdown"
    STR_MQTT_PROT_OVER_CURRENT,     // "UPS Over Current Protection"
    STR_MQTT_PROT_OVER_TEMP,        // "UPS Over Temperature Protection"
    STR_MQTT_PROT_SHORT_CIRCUIT,    // "UPS Short Circuit Protection"
    STR_MQTT_SYSTEM_UPTIME,         // "UPS System Uptime"
    STR_MQTT_BOARD_TEMP,            // "UPS Board Temperature"
    STR_MQTT_ENV_TEMP,              // "UPS Environment Temperature"
    STR_MQTT_SHT_TEMP,              // "UPS SHTC3 Temperature"
    STR_MQTT_SHT_HUMID,             // "UPS SHTC3 Humidity"
    STR_MQTT_WIFI_RSSI,             // "UPS WiFi RSSI"
    STR_MQTT_FIRMWARE_VERSION,      // "UPS Firmware Version"
    STR_MQTT_POWER_MODE,            // "UPS Power Mode"
    STR_MQTT_OVERALL_STATUS,        // "UPS Overall Status"
    STR_MQTT_WIFI_SSID,             // "UPS WiFi SSID"
    STR_MQTT_LED_BRIGHTNESS,        // "UPS LED Brightness"
    STR_MQTT_BUZZER_VOLUME,         // "UPS Buzzer Volume"
    STR_MQTT_HID_REPORT_MODE,       // "UPS HID Report Mode"

    STR_COUNT                       // 字符串总数
};

// =============================================================================
// I18n 类
// =============================================================================
class I18n {
public:
    // 从 NVS 加载语言设置（启动时调用）
    static void loadLanguage();

    // 设置语言并写入 NVS
    static void setLanguage(Language lang);

    // 获取当前语言
    static Language getCurrentLang();

    // 获取当前语言代码字符串（"zh" / "en"）
    static const char* getLangCode();

    // 获取翻译字符串（PROGMEM 读取）
    static const char* get(StrId id);

private:
    static Language currentLang_;
    static const char* const strings_zh_[];
    static const char* const strings_en_[];
};

#endif
