// webhook_presets.h
#pragma once
// Webhook 预置告警模板库
// 内置科学制定的触发值/提醒值，前端一键应用。
// 电压类预置按 cell_count × chemistry 解析静态固定表（不读用户自定义 OV/UV 配置）。

#include <stdint.h>
#include "webhook_types.h"
#include "i18n.h"

// 预置标志位
#define WH_PRESET_FLAG_RESOLVE_VOLTAGE 0x01  // 电压类：按 cell_count × chemistry 解析总电压阈值

// 预置编号（稳定，前端按 id 引用）
enum WebhookPresetId : uint8_t {
    WH_P_SOC_LOW = 0,       // 低电量提醒
    WH_P_SOC_CRIT,          // 电量严重不足
    WH_P_SOH_LOW,           // 电池老化提醒
    WH_P_SOH_CRIT,          // 电池严重老化
    WH_P_TEMP_HIGH,         // 电池高温提醒
    WH_P_TEMP_CRIT,         // 电池高温严重
    WH_P_TEMP_LOW,          // 电池低温提醒
    WH_P_TEMP_LOW_CRIT,     // 电池低温严重
    WH_P_BOARD_HIGH,        // 板温提醒
    WH_P_BOARD_CRIT,        // 板温严重
    WH_P_DISCHG_HIGH,       // 放电过流提醒
    WH_P_DISCHG_CRIT,       // 放电过流严重
    WH_P_CHG_HIGH,          // 充电过流提醒
    WH_P_CHG_CRIT,          // 充电过流严重
    WH_P_IN_LOW,            // 输入欠压提醒
    WH_P_IN_LOW_CRIT,       // 输入欠压严重
    WH_P_IN_HIGH,           // 输入过压提醒
    WH_P_IN_HIGH_CRIT,      // 输入过压严重
    WH_P_OV,                // 电池过压提醒 (电压解析)
    WH_P_OV_CRIT,           // 电池过压严重 (电压解析)
    WH_P_UV,                // 电池欠压提醒 (电压解析)
    WH_P_UV_CRIT,           // 电池欠压严重 (电压解析)
    WH_P_AC_OFF,            // 市电中断
    WH_P_AC_ON,             // 市电恢复
    WH_P_BMS_FAULT,         // 任意 BMS 故障
    WH_P_PWR_FAULT,         // 任意电源故障
    WH_P_EMERGENCY,         // 紧急关机
    WH_P_SYS_CRIT,          // 系统严重状态
    WH_P_SYS_WARN,          // 系统警告状态
    WH_P_PMODE,             // 电源模式切换
    WH_P_CHARGER,           // 充电器状态变化
    WH_P_BALANCE,           // 均衡开始
    WH_P_COUNT
};

typedef struct {
    uint8_t  id;              // WebhookPresetId
    uint8_t  trigger_type;    // WebhookTriggerType_t
    uint8_t  source;          // WebhookValueSource_t / WebhookStateSource_t
    uint8_t  compare_op;      // WebhookCompareOp_t
    float    threshold;       // 值阈值；电压类填 0，运行时解析
    uint8_t  alert_level;     // WebhookAlertLevel_t
    uint16_t title_id;        // StrId 标题
    const char* dedup_key;    // 去重键
    uint8_t  flags;           // WH_PRESET_FLAG_*
} WebhookPreset_t;

uint8_t getWebhookPresetCount(void);
const WebhookPreset_t* getWebhookPreset(uint8_t id);
// 解析预置阈值（电压类按 cell_count × chemistry 静态表解析，其余直接返回）
float resolvePresetThreshold(const WebhookPreset_t* p, uint8_t cell_count, uint8_t chemistry);
// 将预置填充到触发器（解析阈值 + 本地化标题 + 去重键）
void applyPreset(WebhookTrigger_t& trig, uint8_t presetId, uint8_t cellCount, uint8_t chemistry);
