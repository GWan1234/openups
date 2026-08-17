// webhook_types.h
#pragma once
// Webhook 告警推送 - 数据类型定义
// 平台不预置，由用户自定义名称；认证字段分离为 Token 与 Device Key
// 支持值阈值触发和状态变化触发，以及触发→解除生命周期

#include <stdint.h>

// =============================================================================
// 触发器类型
// =============================================================================
typedef enum : uint8_t {
    WH_TRIGGER_VALUE = 0,       // 值阈值触发 (温度/电压/电流/SOC/SOH > 或 <)
    WH_TRIGGER_STATE,           // 状态变化触发 (AC断电/恢复、充电开启/关闭等)
} WebhookTriggerType_t;

// =============================================================================
// 值源 (可监控的实时模拟量)
// =============================================================================
typedef enum : uint8_t {
    WH_VALUE_TEMPERATURE = 0,   // 电池温度 (°C, float)
    WH_VALUE_CURRENT,           // 电池电流 (mA, int16, 正充负放)
    WH_VALUE_VOLTAGE,           // 电池总电压 (mV, uint16)
    WH_VALUE_SOC,               // SOC (%, float, 0-100)
    WH_VALUE_SOH,               // SOH (%, float, 0-100)
    WH_VALUE_INPUT_VOLTAGE,     // 输入电压 (mV, uint16)
    WH_VALUE_BOARD_TEMP,        // 板温 (°C, float)
    WH_VALUE_SOURCE_MAX
} WebhookValueSource_t;

// =============================================================================
// 状态源 (可监控的离散状态)
// =============================================================================
typedef enum : uint8_t {
    WH_STATE_AC_POWER = 0,      // AC 电源 (0=断开, 1=连接)
    WH_STATE_CHARGER,           // 充电器 (0=关闭, 1=开启)
    WH_STATE_BMS_FAULT,         // BMS 故障 (0=无, 1-7=故障类型)
    WH_STATE_POWER_FAULT,       // 电源故障 (0=无, 1-10=故障类型)
    WH_STATE_FSM_STATE,         // FSM 状态 (0=INIT, 1=NORMAL, 2=WARNING, 3=CRITICAL)
    WH_STATE_POWER_MODE,        // 电源模式 (0=AC, 1=BATTERY, 2=HYBRID, 3=CHARGING)
    WH_STATE_EMERGENCY,         // 紧急关机 (0=正常, 1=紧急关机)
    WH_STATE_BALANCING,         // 均衡 (0=停止, 1=进行中)
    WH_STATE_SOURCE_MAX
} WebhookStateSource_t;

// =============================================================================
// 比较运算符
// =============================================================================
typedef enum : uint8_t {
    WH_CMP_GT = 0,              // > (值触发: 当前值 > 阈值)
    WH_CMP_LT,                  // < (值触发: 当前值 < 阈值)
    WH_CMP_EQ,                  // == (状态触发: 当前状态 == 目标值)
    WH_CMP_CHANGE,              // 变化 (状态触发: 状态发生任何变化)
    WH_CMP_NE,                  // != (状态触发: 当前状态 != 目标值, 用于"任意故障"类)
} WebhookCompareOp_t;

// =============================================================================
// 告警级别
// =============================================================================
typedef enum : uint8_t {
    WH_LEVEL_INFO = 0,          // 信息
    WH_LEVEL_WARNING,           // 警告
    WH_LEVEL_CRITICAL,          // 严重
} WebhookAlertLevel_t;

// =============================================================================
// 请求方式
// =============================================================================
typedef enum : uint8_t {
    WH_METHOD_POST = 0,         // POST JSON 体 (默认)
    WH_METHOD_GET,              // GET (消息通过 URL 标签传递)
} WebhookMethod_t;

// =============================================================================
// 端点和触发器限制
// =============================================================================
#define WH_MAX_ENDPOINTS        5       // 最多端点数
#define WH_MAX_TRIGGERS         3       // 每端点最多触发器数
#define WH_QUEUE_SIZE           8       // FreeRTOS 队列深度
#define WH_TASK_STACK_SIZE      12288   // 后台任务栈 (HTTPS/TLS 握手 + 838B 端点结构体，需较大栈)
#define WH_TASK_PRIORITY        3       // 任务优先级 (低于主循环)
#define WH_DEFAULT_COOLDOWN     60000   // 默认冷却 60 秒
#define WH_HTTP_TIMEOUT         10000   // HTTP 超时 10 秒
#define WH_MAX_RETRIES          3       // 最大重试次数

// 迟滞常量 (防止阈值附近抖动)
#define WH_HYSTERESIS_TEMP      2.0f    // 温度 2°C
#define WH_HYSTERESIS_MV        50      // 电压 50mV
#define WH_HYSTERESIS_MA        100     // 电流 100mA
#define WH_HYSTERESIS_PCT       2.0f    // SOC/SOH 2%

// 字段长度限制 (NVS 4KB 限制下紧凑设计)
#define WH_NAME_MAX_LEN         32
#define WH_URL_MAX_LEN          80
#define WH_TOKEN_MAX_LEN        48
#define WH_KEY_MAX_LEN          40
#define WH_TITLE_MAX_LEN        48
#define WH_DESC_MAX_LEN         64
#define WH_TEMPLATE_MAX_LEN     96
#define WH_HEADER_MAX_LEN       40

// 配置版本
#define WH_CONFIG_VERSION       1

// =============================================================================
// 触发条件 (packed, 8 bytes)
// =============================================================================
typedef struct __attribute__((packed)) {
    uint8_t trigger_type;               // WebhookTriggerType_t
    uint8_t source;                     // WebhookValueSource_t 或 WebhookStateSource_t
    uint8_t compare_op;                 // WebhookCompareOp_t
    uint8_t reserved;                   // 对齐填充
    float threshold;                    // 阈值 (值触发) 或目标状态值 (状态触发)
} WebhookCondition_t;                   // 8 bytes

// =============================================================================
// 触发器 (packed, 165 bytes)
// =============================================================================
typedef struct __attribute__((packed)) {
    bool enabled;                               // 1  是否启用
    uint8_t alert_level;                        // 1  WebhookAlertLevel_t
    WebhookCondition_t condition;               // 8  触发条件
    char dedup_key[WH_KEY_MAX_LEN];             // 40 去重键 (如 "UPS_AC_POWER")
    char title[WH_TITLE_MAX_LEN];               // 48 标题模板 (如 "UPS 电力中断")
    char description[WH_DESC_MAX_LEN];          // 64 描述模板
    bool fired;                                 // 1  运行时: 当前是否已触发
    uint8_t reserved[2];                        // 2  对齐
} WebhookTrigger_t;                             // 165 bytes

// =============================================================================
// 端点 (packed, 838 bytes)
// =============================================================================
typedef struct __attribute__((packed)) {
    bool enabled;                               // 1  是否启用
    bool verify_tls;                            // 1  是否校验 HTTPS 证书 (内置根证书库)
    uint8_t method;                             // 1  请求方式 (WebhookMethod_t: 0=POST, 1=GET)
    char name[WH_NAME_MAX_LEN];                 // 32 自定义名 (端点标识，前台显示)
    char url[WH_URL_MAX_LEN];                   // 80 API URL (支持 {title}/{token} 等标签替换)
    char auth_token[WH_TOKEN_MAX_LEN];          // 48 认证 Token ({token} 标签)
    char device_key[WH_KEY_MAX_LEN];            // 40 设备密钥 ({key} 标签)
    char auth_header[WH_HEADER_MAX_LEN];        // 40 认证 Header (支持 {token}/{key} 标签替换)
    uint32_t cooldown_ms;                       // 4  冷却时间 (ms)
    char message_template[WH_TEMPLATE_MAX_LEN]; // 96 端点级消息模板 (可选覆盖)
    uint8_t trigger_count;                      // 1  有效触发器数
    WebhookTrigger_t triggers[WH_MAX_TRIGGERS]; // 3 × 165 = 495
} WebhookEndpoint_t;                            // 839 bytes

// =============================================================================
// 总配置 (SPIFFS 文件 /webhook_cfg.json，JSON 序列化持久化)
// 原设计存 NVS "wh_cfg" 超出 20KB NVS 分区可用连续空间，已迁移至 SPIFFS
// =============================================================================
typedef struct __attribute__((packed)) {
    uint16_t config_version;                    // 2
    bool global_enabled;                        // 1
    uint8_t endpoint_count;                     // 1
    WebhookEndpoint_t endpoints[WH_MAX_ENDPOINTS]; // 5 × 839 = 4195
} WebhookConfig_t;                              // 4199 bytes

// =============================================================================
// FreeRTOS 队列项 (运行时，不存储到 NVS)
// =============================================================================
typedef struct {
    uint8_t endpoint_index;                     // 目标端点索引 (0-4)
    uint8_t trigger_index;                      // 触发器索引 (0-2)
    uint8_t action;                             // 0=trigger, 1=resolve, 2=test
    uint8_t alert_level;                        // WebhookAlertLevel_t
    uint32_t timestamp;                         // millis()
    char dedup_key[WH_KEY_MAX_LEN];             // 40
    char title[WH_TITLE_MAX_LEN];               // 48
    char message[WH_DESC_MAX_LEN];              // 64
} WebhookQueueItem_t;                           // ~168 bytes

// =============================================================================
// 遥测快照 (运行时，供后台任务模板变量替换，不存储到 NVS)
// =============================================================================
typedef struct {
    float soc;                                  // SOC (%)
    float soh;                                  // SOH (%)
    float temperature;                          // 电池温度 (°C)
    int16_t current;                            // 电池电流 (mA)
    uint16_t voltage;                           // 电池总电压 (mV)
    bool ac_present;                            // AC 电源存在
} WebhookTelemetry_t;

// =============================================================================
// 端点运行时统计 (不存储到 NVS)
// =============================================================================
typedef struct {
    uint32_t total_sent;                        // 总发送次数
    uint32_t total_failed;                      // 总失败次数
    uint32_t total_resolved;                    // 总解除次数
    uint32_t last_sent_ms;                      // 上次发送完成时间
    uint32_t last_trigger_ms;                   // 上次触发入队时间 (冷却判定)
    bool last_success;                          // 上次是否成功
} WebhookEndpointStats_t;
