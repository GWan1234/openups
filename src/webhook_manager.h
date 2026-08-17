// webhook_manager.h
#pragma once
// Webhook 告警推送管理器
// 平台不预置，通用 POST JSON 推送；Token 与 Device Key 分离
// 值阈值触发 + 状态变化触发 + 触发→解除生命周期

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "webhook_types.h"
#include "data_structures.h"

// 前向声明
class ConfigManager;

// FreeRTOS 队列动作
#define WH_ACTION_TRIGGER   0
#define WH_ACTION_RESOLVE   1
#define WH_ACTION_TEST      2

class WebhookManager {
public:
    WebhookManager(ConfigManager& configMgr);
    ~WebhookManager();

    // 初始化: 加载配置、创建队列和后台任务
    bool begin();

    // 触发器求值 (每秒由 SystemManagement 调用)
    void evaluateTriggers(const System_Global_State& state);

    // 更新 FSM 状态缓存 (由 SystemManagement 调用)
    void setFsmState(uint8_t state) { currentFsmState_ = state; }

    // 配置访问 (返回副本，线程安全)
    WebhookConfig_t getConfig() const;
    bool updateConfig(const WebhookConfig_t& newConfig, char* reason = nullptr);
    bool loadConfig();
    bool saveConfig();
    void loadDefaults();

    // 测试端点 (同步发送，阻塞直到完成，返回真实 HTTP 结果)
    bool sendTestNow(uint8_t index, String& responseMsg, int& httpCode);

    // 获取端点统计 (返回副本，线程安全)
    bool getEndpointStats(uint8_t index, WebhookEndpointStats_t& out) const;

private:
    static WebhookManager* instance_;
    ConfigManager& configManager_;
    WebhookConfig_t config_;
    WebhookEndpointStats_t stats_[WH_MAX_ENDPOINTS];

    // 并发保护: config_ / stats_ / telemetry_ 读写锁
    SemaphoreHandle_t configMutex_;

    // FreeRTOS 队列和任务
    QueueHandle_t webhookQueue_;
    TaskHandle_t webhookTaskHandle_;

    // FSM 状态缓存
    uint8_t currentFsmState_;

    // 状态变化触发: 上一帧状态缓存 (运行时)
    uint8_t prevState_[WH_MAX_ENDPOINTS][WH_MAX_TRIGGERS];
    bool prevStateValid_[WH_MAX_ENDPOINTS][WH_MAX_TRIGGERS];

    // 遥测快照 (供后台任务模板变量替换)
    WebhookTelemetry_t telemetry_;

    // 触发器求值辅助
    void evaluateValueTrigger(WebhookEndpoint_t& ep, uint8_t trigIdx,
                              const System_Global_State& state);
    void evaluateStateTrigger(WebhookEndpoint_t& ep, uint8_t trigIdx,
                              const System_Global_State& state);

    // 值/状态获取
    float getTriggerValue(uint8_t source, const System_Global_State& state);
    uint8_t getTriggerState(uint8_t source, const System_Global_State& state);
    float getHysteresis(uint8_t source);
    const char* getLevelString(uint8_t level);
    const char* getStateString(uint8_t source, uint8_t value);

    // 队列操作 (内部调用，调用方需持有 configMutex_)
    bool enqueueWebhookLocked(uint8_t epIdx, uint8_t trigIdx, uint8_t action,
                              const char* title, const char* desc, uint8_t level);

    // FreeRTOS 后台任务
    static void webhookTaskFunc(void* pvParameters);

    // HTTP 发送 (httpCodeOut 可选，输出远端 HTTP 状态码)
    bool sendWebhook(const WebhookQueueItem_t& item, String& responseMsg, int* httpCodeOut = nullptr);

    // 通用 Payload 构建
    String buildPayload(const WebhookQueueItem_t& item, const WebhookEndpoint_t& ep,
                        const WebhookTelemetry_t& tel);

    // 模板变量替换 ({token} → token, {key} → key)
    // urlEncode=true 时对每个替换值做 URL 编码 (用于 URL 标签，GET 场景)
    String substituteVariables(const String& tpl, const WebhookQueueItem_t& item,
                               const WebhookTelemetry_t& tel, const char* token, const char* key,
                               bool urlEncode = false);

    // 冷却检查 (调用方需持有 configMutex_)
    bool isCoolingDown(uint8_t epIdx);
};
