// webhook_presets.cpp
#include "webhook_presets.h"
#include "battery_chemistry.h"
#include <string.h>

// =============================================================================
// 预置告警模板目录
//
// 阈值科学依据（对齐代码锚点，详见 docs/webhook-alert-preset-design.md）：
//   SOC 20%/10%           ← system_management.h SOC_LOW_THRESHOLD / SOC_CRITICAL_THRESHOLD
//   电池温度 55/65/5/-10  ← charge_temp_high_limit / temp_overheat_threshold / charge_temp_low_limit
//   板温 65/75            ← over_temp_threshold
//   电流 -10000/-12000    ← power/BMS max_discharge_current；+2000/+8064 ← max_charge_current / over_current_threshold
//   输入电压 12/11/19/20V ← 额定 12-19V 输入
//   总电压                ← 化学推荐单体 OV/UV (NCM 4210/3000, LFP 3650/2500) × 串数
// =============================================================================

static const WebhookPreset_t WH_PRESETS[WH_P_COUNT] = {
    // --- SOC ---
    { WH_P_SOC_LOW,    WH_TRIGGER_VALUE, WH_VALUE_SOC,       WH_CMP_LT, 20.0f,  WH_LEVEL_WARNING,  STR_WH_P_SOC_LOW,      "UPS_SOC_LOW",      0 },
    { WH_P_SOC_CRIT,   WH_TRIGGER_VALUE, WH_VALUE_SOC,       WH_CMP_LT, 10.0f,  WH_LEVEL_CRITICAL, STR_WH_P_SOC_CRIT,     "UPS_SOC_CRIT",     0 },
    // --- SOH ---
    { WH_P_SOH_LOW,    WH_TRIGGER_VALUE, WH_VALUE_SOH,       WH_CMP_LT, 80.0f,  WH_LEVEL_WARNING,  STR_WH_P_SOH_LOW,      "UPS_SOH_LOW",      0 },
    { WH_P_SOH_CRIT,   WH_TRIGGER_VALUE, WH_VALUE_SOH,       WH_CMP_LT, 70.0f,  WH_LEVEL_CRITICAL, STR_WH_P_SOH_CRIT,     "UPS_SOH_CRIT",     0 },
    // --- 电池温度 ---
    { WH_P_TEMP_HIGH,  WH_TRIGGER_VALUE, WH_VALUE_TEMPERATURE, WH_CMP_GT, 55.0f, WH_LEVEL_WARNING,  STR_WH_P_TEMP_HIGH,    "UPS_TEMP_HIGH",    0 },
    { WH_P_TEMP_CRIT,  WH_TRIGGER_VALUE, WH_VALUE_TEMPERATURE, WH_CMP_GT, 65.0f, WH_LEVEL_CRITICAL, STR_WH_P_TEMP_CRIT,    "UPS_TEMP_CRIT",    0 },
    { WH_P_TEMP_LOW,   WH_TRIGGER_VALUE, WH_VALUE_TEMPERATURE, WH_CMP_LT, 5.0f,  WH_LEVEL_WARNING,  STR_WH_P_TEMP_LOW,     "UPS_TEMP_LOW",     0 },
    { WH_P_TEMP_LOW_CRIT, WH_TRIGGER_VALUE, WH_VALUE_TEMPERATURE, WH_CMP_LT, -10.0f, WH_LEVEL_CRITICAL, STR_WH_P_TEMP_LOW_CRIT, "UPS_TEMP_LOW_CRIT", 0 },
    // --- 板温 ---
    { WH_P_BOARD_HIGH, WH_TRIGGER_VALUE, WH_VALUE_BOARD_TEMP, WH_CMP_GT, 65.0f, WH_LEVEL_WARNING,  STR_WH_P_BOARD_HIGH,   "UPS_BOARD_HIGH",   0 },
    { WH_P_BOARD_CRIT, WH_TRIGGER_VALUE, WH_VALUE_BOARD_TEMP, WH_CMP_GT, 75.0f, WH_LEVEL_CRITICAL, STR_WH_P_BOARD_CRIT,   "UPS_BOARD_CRIT",   0 },
    // --- 电池电流 (正充负放) ---
    { WH_P_DISCHG_HIGH, WH_TRIGGER_VALUE, WH_VALUE_CURRENT, WH_CMP_LT, -10000.0f, WH_LEVEL_WARNING,  STR_WH_P_DISCHG_HIGH, "UPS_DISCHG_HIGH", 0 },
    { WH_P_DISCHG_CRIT, WH_TRIGGER_VALUE, WH_VALUE_CURRENT, WH_CMP_LT, -12000.0f, WH_LEVEL_CRITICAL, STR_WH_P_DISCHG_CRIT, "UPS_DISCHG_CRIT", 0 },
    { WH_P_CHG_HIGH,   WH_TRIGGER_VALUE, WH_VALUE_CURRENT, WH_CMP_GT, 2000.0f,  WH_LEVEL_WARNING,  STR_WH_P_CHG_HIGH,     "UPS_CHG_HIGH",     0 },
    { WH_P_CHG_CRIT,   WH_TRIGGER_VALUE, WH_VALUE_CURRENT, WH_CMP_GT, 8064.0f,  WH_LEVEL_CRITICAL, STR_WH_P_CHG_CRIT,     "UPS_CHG_CRIT",     0 },
    // --- 输入电压 (额定 12-19V) ---
    { WH_P_IN_LOW,     WH_TRIGGER_VALUE, WH_VALUE_INPUT_VOLTAGE, WH_CMP_LT, 12000.0f, WH_LEVEL_WARNING,  STR_WH_P_IN_LOW,    "UPS_IN_LOW",       0 },
    { WH_P_IN_LOW_CRIT, WH_TRIGGER_VALUE, WH_VALUE_INPUT_VOLTAGE, WH_CMP_LT, 11000.0f, WH_LEVEL_CRITICAL, STR_WH_P_IN_LOW_CRIT, "UPS_IN_LOW_CRIT", 0 },
    { WH_P_IN_HIGH,    WH_TRIGGER_VALUE, WH_VALUE_INPUT_VOLTAGE, WH_CMP_GT, 19000.0f, WH_LEVEL_WARNING,  STR_WH_P_IN_HIGH,   "UPS_IN_HIGH",      0 },
    { WH_P_IN_HIGH_CRIT, WH_TRIGGER_VALUE, WH_VALUE_INPUT_VOLTAGE, WH_CMP_GT, 20000.0f, WH_LEVEL_CRITICAL, STR_WH_P_IN_HIGH_CRIT, "UPS_IN_HIGH_CRIT", 0 },
    // --- 电池总电压 (运行时按 cell_count × chemistry 解析) ---
    { WH_P_OV,         WH_TRIGGER_VALUE, WH_VALUE_VOLTAGE, WH_CMP_GT, 0.0f, WH_LEVEL_WARNING,  STR_WH_P_OV,       "UPS_OV",       WH_PRESET_FLAG_RESOLVE_VOLTAGE },
    { WH_P_OV_CRIT,    WH_TRIGGER_VALUE, WH_VALUE_VOLTAGE, WH_CMP_GT, 0.0f, WH_LEVEL_CRITICAL, STR_WH_P_OV_CRIT,  "UPS_OV_CRIT",  WH_PRESET_FLAG_RESOLVE_VOLTAGE },
    { WH_P_UV,         WH_TRIGGER_VALUE, WH_VALUE_VOLTAGE, WH_CMP_LT, 0.0f, WH_LEVEL_WARNING,  STR_WH_P_UV,       "UPS_UV",       WH_PRESET_FLAG_RESOLVE_VOLTAGE },
    { WH_P_UV_CRIT,    WH_TRIGGER_VALUE, WH_VALUE_VOLTAGE, WH_CMP_LT, 0.0f, WH_LEVEL_CRITICAL, STR_WH_P_UV_CRIT,  "UPS_UV_CRIT",  WH_PRESET_FLAG_RESOLVE_VOLTAGE },
    // --- 状态触发 ---
    { WH_P_AC_OFF,     WH_TRIGGER_STATE, WH_STATE_AC_POWER,   WH_CMP_EQ, 0.0f, WH_LEVEL_CRITICAL, STR_WH_P_AC_OFF,   "UPS_AC_OFF",   0 },
    { WH_P_AC_ON,      WH_TRIGGER_STATE, WH_STATE_AC_POWER,   WH_CMP_EQ, 1.0f, WH_LEVEL_INFO,     STR_WH_P_AC_ON,    "UPS_AC_ON",    0 },
    { WH_P_BMS_FAULT,  WH_TRIGGER_STATE, WH_STATE_BMS_FAULT,  WH_CMP_NE, 0.0f, WH_LEVEL_CRITICAL, STR_WH_P_BMS_FAULT, "UPS_BMS_FAULT", 0 },
    { WH_P_PWR_FAULT,  WH_TRIGGER_STATE, WH_STATE_POWER_FAULT,WH_CMP_NE, 0.0f, WH_LEVEL_WARNING,  STR_WH_P_PWR_FAULT, "UPS_PWR_FAULT", 0 },
    { WH_P_EMERGENCY,  WH_TRIGGER_STATE, WH_STATE_EMERGENCY,  WH_CMP_EQ, 1.0f, WH_LEVEL_CRITICAL, STR_WH_P_EMERGENCY, "UPS_EMERGENCY", 0 },
    { WH_P_SYS_CRIT,   WH_TRIGGER_STATE, WH_STATE_FSM_STATE,  WH_CMP_EQ, 3.0f, WH_LEVEL_CRITICAL, STR_WH_P_SYS_CRIT,  "UPS_SYS_CRIT", 0 },
    { WH_P_SYS_WARN,   WH_TRIGGER_STATE, WH_STATE_FSM_STATE,  WH_CMP_EQ, 2.0f, WH_LEVEL_WARNING,  STR_WH_P_SYS_WARN,  "UPS_SYS_WARN", 0 },
    { WH_P_PMODE,      WH_TRIGGER_STATE, WH_STATE_POWER_MODE, WH_CMP_CHANGE, 0.0f, WH_LEVEL_INFO, STR_WH_P_PMODE,    "UPS_PMODE",    0 },
    { WH_P_CHARGER,    WH_TRIGGER_STATE, WH_STATE_CHARGER,    WH_CMP_CHANGE, 0.0f, WH_LEVEL_INFO, STR_WH_P_CHARGER,  "UPS_CHARGER",  0 },
    { WH_P_BALANCE,    WH_TRIGGER_STATE, WH_STATE_BALANCING,  WH_CMP_EQ, 1.0f, WH_LEVEL_INFO,     STR_WH_P_BALANCE,  "UPS_BALANCE",  0 },
};

uint8_t getWebhookPresetCount(void) {
    return WH_P_COUNT;
}

const WebhookPreset_t* getWebhookPreset(uint8_t id) {
    if (id >= WH_P_COUNT) return nullptr;
    return &WH_PRESETS[id];
}

float resolvePresetThreshold(const WebhookPreset_t* p, uint8_t cell_count, uint8_t chemistry) {
    if (!p) return 0.0f;
    if (!(p->flags & WH_PRESET_FLAG_RESOLVE_VOLTAGE)) return p->threshold;

    if (cell_count < 3) cell_count = 3;
    if (cell_count > 5) cell_count = 5;
    bool lfp = (chemistry == (uint8_t)CHEM_LFP);

    // 化学推荐单体阈值（静态固定表，不读用户自定义 OV/UV 配置）
    uint16_t cell_mv;
    switch (p->id) {
        case WH_P_OV:      cell_mv = lfp ? 3550 : 4100; break; // 过压提醒
        case WH_P_OV_CRIT: cell_mv = lfp ? 3650 : 4210; break; // 过压严重
        case WH_P_UV:      cell_mv = lfp ? 2600 : 3100; break; // 欠压提醒
        case WH_P_UV_CRIT: cell_mv = lfp ? 2500 : 3000; break; // 欠压严重
        default:           return p->threshold;
    }
    return (float)((uint32_t)cell_mv * cell_count);
}

void applyPreset(WebhookTrigger_t& trig, uint8_t presetId, uint8_t cellCount, uint8_t chemistry) {
    memset(&trig, 0, sizeof(trig));
    const WebhookPreset_t* p = getWebhookPreset(presetId);
    if (!p) return;

    trig.enabled = true;
    trig.alert_level = p->alert_level;
    trig.condition.trigger_type = p->trigger_type;
    trig.condition.source = p->source;
    trig.condition.compare_op = p->compare_op;
    trig.condition.threshold = resolvePresetThreshold(p, cellCount, chemistry);
    strlcpy(trig.dedup_key, p->dedup_key, sizeof(trig.dedup_key));
    strlcpy(trig.title, I18n::get((StrId)p->title_id), sizeof(trig.title));
    trig.description[0] = '\0';
    trig.fired = false;
}
