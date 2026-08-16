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
    STR_MQTT_CELL_IR,               // "UPS Cell %d Internal Resistance"
    STR_MQTT_IR_SAMPLE_COUNT,       // "UPS IR Sample Count"

    // === Webhook：API 响应 ===
    STR_WH_NOT_INIT,                // "Webhook 模块未初始化"
    STR_WH_MISSING_BODY,            // "缺少请求体"
    STR_WH_BAD_JSON,                // "JSON 解析失败"
    STR_WH_INVALID_INDEX,           // "无效端点索引"
    STR_WH_VALIDATE_FAIL,           // "配置验证失败"
    STR_WH_SAVE_FAIL,               // "配置保存失败"
    STR_WH_SAVED,                   // "Webhook 配置已保存"

    // === Webhook：测试发送 ===
    STR_WH_WIFI_DOWN,               // "WiFi 未连接"
    STR_WH_ENDPOINT_DISABLED,       // "端点未启用"
    STR_WH_TEST_TITLE,              // "Webhook 测试"
    STR_WH_TEST_MSG,                // "这是一条 Webhook 测试消息，来自 ESP32-S3 UPS"

    // === Webhook：触发器消息模板 ===
    STR_WH_VAL_GT,                  // "%s 当前 %.1f 超过阈值 %.1f"
    STR_WH_VAL_LT,                  // "%s 当前 %.1f 低于阈值 %.1f"
    STR_WH_VAL_RECOVER,             // "%s 已恢复，当前 %.1f"
    STR_WH_STATE_RECOVER,           // "%s 已恢复: %s"

    // === Webhook：状态字符串 (getStateString) ===
    STR_WH_ST_AC_ON,                // "AC 已连接"
    STR_WH_ST_AC_OFF,               // "AC 已断开"
    STR_WH_ST_CHG_ON,               // "充电已开启"
    STR_WH_ST_CHG_OFF,              // "充电已关闭"
    STR_WH_ST_BMS_FAULT,            // "BMS 故障"
    STR_WH_ST_BMS_NORMAL,           // "BMS 正常"
    STR_WH_ST_PWR_FAULT,            // "电源故障"
    STR_WH_ST_PWR_NORMAL,           // "电源正常"
    STR_WH_ST_FSM_INIT,             // "初始化"
    STR_WH_ST_FSM_NORMAL,           // "正常"
    STR_WH_ST_FSM_WARNING,          // "警告"
    STR_WH_ST_FSM_CRITICAL,         // "严重"
    STR_WH_ST_PM_AC,                // "AC 供电"
    STR_WH_ST_PM_BATTERY,           // "电池供电"
    STR_WH_ST_PM_HYBRID,            // "混合供电"
    STR_WH_ST_PM_CHARGING,          // "充电中"
    STR_WH_ST_EMERGENCY,            // "紧急关机"
    STR_WH_ST_BALANCING,            // "均衡中"
    STR_WH_ST_BAL_STOP,             // "均衡停止"
    STR_WH_ST_UNKNOWN,              // "未知"

    // === Webhook：模板变量 ===
    STR_WH_AC_ON,                   // "连接"
    STR_WH_AC_OFF,                  // "断开"

    // === Webhook：配置验证失败原因 ===
    STR_WH_V_VERSION,               // "配置版本不匹配"
    STR_WH_V_ENDPOINT_COUNT,        // "端点数量超限"
    STR_WH_V_URL_EMPTY,             // "端点 %u URL 不能为空"
    STR_WH_V_METHOD,                // "端点 %u 请求方式无效"
    STR_WH_V_COOLDOWN,              // "端点 %u 冷却时间不能小于 10 秒"
    STR_WH_V_TRIGGER_COUNT,         // "端点 %u 触发器数量超限"
    STR_WH_V_TRIGGER_TYPE,          // "端点 %u 触发器类型无效"
    STR_WH_V_CMP_OP,                // "端点 %u 比较运算符无效"
    STR_WH_V_VALUE_OP,              // "端点 %u 值触发只能用大于/小于"
    STR_WH_V_STATE_OP,              // "端点 %u 状态触发只能用等于/变化/不等于"
    STR_WH_V_SOURCE_VALUE,          // "端点 %u 监测值无效"
    STR_WH_V_SOURCE_STATE,          // "端点 %u 监测状态无效"
    STR_WH_V_LEVEL,                 // "端点 %u 告警级别无效"

    // === Webhook：预置告警模板标题 (preset titles) ===
    STR_WH_P_SOC_LOW,               // "低电量提醒"
    STR_WH_P_SOC_CRIT,              // "电量严重不足"
    STR_WH_P_SOH_LOW,               // "电池老化提醒"
    STR_WH_P_SOH_CRIT,              // "电池严重老化"
    STR_WH_P_TEMP_HIGH,             // "电池高温提醒"
    STR_WH_P_TEMP_CRIT,             // "电池高温严重"
    STR_WH_P_TEMP_LOW,              // "电池低温提醒"
    STR_WH_P_TEMP_LOW_CRIT,         // "电池低温严重"
    STR_WH_P_BOARD_HIGH,            // "板温提醒"
    STR_WH_P_BOARD_CRIT,            // "板温严重"
    STR_WH_P_DISCHG_HIGH,           // "放电过流提醒"
    STR_WH_P_DISCHG_CRIT,           // "放电过流严重"
    STR_WH_P_CHG_HIGH,              // "充电过流提醒"
    STR_WH_P_CHG_CRIT,              // "充电过流严重"
    STR_WH_P_IN_LOW,                // "输入欠压提醒"
    STR_WH_P_IN_LOW_CRIT,           // "输入欠压严重"
    STR_WH_P_IN_HIGH,               // "输入过压提醒"
    STR_WH_P_IN_HIGH_CRIT,          // "输入过压严重"
    STR_WH_P_OV,                    // "电池过压提醒"
    STR_WH_P_OV_CRIT,               // "电池过压严重"
    STR_WH_P_UV,                    // "电池欠压提醒"
    STR_WH_P_UV_CRIT,               // "电池欠压严重"
    STR_WH_P_AC_OFF,                // "市电中断"
    STR_WH_P_AC_ON,                 // "市电恢复"
    STR_WH_P_BMS_FAULT,             // "任意 BMS 故障"
    STR_WH_P_PWR_FAULT,             // "任意电源故障"
    STR_WH_P_EMERGENCY,             // "紧急关机"
    STR_WH_P_SYS_CRIT,              // "系统严重状态"
    STR_WH_P_SYS_WARN,              // "系统警告状态"
    STR_WH_P_PMODE,                 // "电源模式切换"
    STR_WH_P_CHARGER,               // "充电器状态变化"
    STR_WH_P_BALANCE,               // "均衡开始"

    // === Webhook：故障名称 (fault names) ===
    STR_WH_F_BMS_OV,                // "过压"
    STR_WH_F_BMS_UV,                // "欠压"
    STR_WH_F_BMS_OC,                // "过流"
    STR_WH_F_BMS_SC,                // "短路"
    STR_WH_F_BMS_OT,                // "过温"
    STR_WH_F_BMS_CHIP,              // "芯片错误"
    STR_WH_F_BMS_PASSIVE,           // "被动关机"
    STR_WH_F_PWR_CHIP,              // "芯片错误"
    STR_WH_F_PWR_OC,                // "过流"
    STR_WH_F_PWR_OT,                // "过温"
    STR_WH_F_PWR_IN_OV,             // "输入过压"
    STR_WH_F_PWR_IN_UV,             // "输入欠压"
    STR_WH_F_PWR_BAT_OV,            // "电池过压"
    STR_WH_F_PWR_BAT_UV,            // "电池欠压"
    STR_WH_F_PWR_SC,                // "短路"
    STR_WH_F_PWR_TIMEOUT,           // "充电超时"
    STR_WH_F_PWR_I2C,               // "I2C通信错误"

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
