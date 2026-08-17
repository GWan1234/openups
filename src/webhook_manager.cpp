// webhook_manager.cpp
#include "webhook_manager.h"
#include "config_manager.h"
#include "i18n.h"
#include "debug.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>
#include "ssl_client.h"

// 启用 ESP32 内置根证书库做 TLS 证书校验 (verify_tls 开启时使用)
class WebhookSecureClient : public WiFiClientSecure {
public:
    void useBuiltinCABundle() {
        _use_ca_bundle = true;
        attach_ssl_certificate_bundle(sslclient.get(), true);
    }
};

WebhookManager* WebhookManager::instance_ = nullptr;

WebhookManager::WebhookManager(ConfigManager& configMgr)
    : configManager_(configMgr)
    , webhookQueue_(nullptr)
    , webhookTaskHandle_(nullptr)
    , configMutex_(xSemaphoreCreateMutex())
    , currentFsmState_(1) // SYS_STATE_NORMAL
{
    instance_ = this;
    memset(&config_, 0, sizeof(config_));
    memset(stats_, 0, sizeof(stats_));
    memset(prevState_, 0, sizeof(prevState_));
    memset(prevStateValid_, 0, sizeof(prevStateValid_));
    memset(&telemetry_, 0, sizeof(telemetry_));
}

WebhookManager::~WebhookManager() {
    if (webhookTaskHandle_) {
        vTaskDelete(webhookTaskHandle_);
        webhookTaskHandle_ = nullptr;
    }
    if (webhookQueue_) {
        vQueueDelete(webhookQueue_);
        webhookQueue_ = nullptr;
    }
    if (configMutex_) {
        vSemaphoreDelete(configMutex_);
        configMutex_ = nullptr;
    }
    instance_ = nullptr;
}

bool WebhookManager::begin() {
    if (!configMutex_) {
        DBG.println(F("[Webhook] 互斥锁创建失败"));
        return false;
    }

    // 加载配置
    if (!loadConfig()) {
        DBG.println(F("[Webhook] 无已存配置，加载默认值"));
        loadDefaults();
    }

    // 创建 FreeRTOS 队列
    webhookQueue_ = xQueueCreate(WH_QUEUE_SIZE, sizeof(WebhookQueueItem_t));
    if (!webhookQueue_) {
        DBG.println(F("[Webhook] 队列创建失败"));
        return false;
    }

    // 创建后台任务 (核心 0，不阻塞主循环所在的核心 1)
    BaseType_t ret = xTaskCreatePinnedToCore(
        webhookTaskFunc,
        "webhook_task",
        WH_TASK_STACK_SIZE,
        this,
        WH_TASK_PRIORITY,
        &webhookTaskHandle_,
        0  // 核心 0
    );

    if (ret != pdPASS) {
        DBG.println(F("[Webhook] 任务创建失败"));
        return false;
    }

    DBG.printf("[Webhook] 初始化完成: %u 端点, 全局%s\n",
               config_.endpoint_count, config_.global_enabled ? "启用" : "禁用");
    return true;
}

// =============================================================================
// 配置管理
// =============================================================================

bool WebhookManager::loadConfig() {
    return configManager_.loadWebhookConfig(config_);
}

bool WebhookManager::saveConfig() {
    WebhookConfig_t copy;
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    copy = config_;
    xSemaphoreGive(configMutex_);
    return configManager_.saveWebhookConfig(copy);
}

void WebhookManager::loadDefaults() {
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    configManager_.loadWebhookDefaults(config_);
    xSemaphoreGive(configMutex_);
}

WebhookConfig_t WebhookManager::getConfig() const {
    WebhookConfig_t copy;
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    copy = config_;
    xSemaphoreGive(configMutex_);
    return copy;
}

bool WebhookManager::updateConfig(const WebhookConfig_t& newConfig, char* reason) {
    if (!configManager_.validateWebhookConfig(newConfig, reason)) {
        DBG.println(F("[Webhook] 配置验证失败"));
        return false;
    }

    xSemaphoreTake(configMutex_, portMAX_DELAY);
    config_ = newConfig;
    // 清除所有触发器的 fired 状态 (新配置重新开始)
    for (uint8_t i = 0; i < config_.endpoint_count; i++) {
        for (uint8_t j = 0; j < config_.endpoints[i].trigger_count; j++) {
            config_.endpoints[i].triggers[j].fired = false;
        }
    }
    // 重置状态变化边缘检测缓存
    memset(prevState_, 0, sizeof(prevState_));
    memset(prevStateValid_, 0, sizeof(prevStateValid_));
    xSemaphoreGive(configMutex_);

    bool ok = saveConfig();
    if (ok) {
        DBG.printf("[Webhook] 配置已更新: %u 端点\n", newConfig.endpoint_count);
    }
    return ok;
}

// =============================================================================
// 触发器求值 (主循环每秒调用)
// =============================================================================

void WebhookManager::evaluateTriggers(const System_Global_State& state) {
    if (!config_.global_enabled) return;

    xSemaphoreTake(configMutex_, portMAX_DELAY);

    // 更新遥测快照，供后台任务模板变量替换
    telemetry_.soc = state.bms.soc;
    telemetry_.soh = state.bms.soh;
    telemetry_.temperature = state.bms.temperature;
    telemetry_.current = state.bms.current;
    telemetry_.voltage = state.bms.voltage;
    telemetry_.ac_present = state.power.ac_present;

    for (uint8_t i = 0; i < config_.endpoint_count && i < WH_MAX_ENDPOINTS; i++) {
        WebhookEndpoint_t& ep = config_.endpoints[i];
        if (!ep.enabled) continue;

        for (uint8_t j = 0; j < ep.trigger_count && j < WH_MAX_TRIGGERS; j++) {
            WebhookTrigger_t& trig = ep.triggers[j];
            if (!trig.enabled) continue;

            if (trig.condition.trigger_type == WH_TRIGGER_VALUE) {
                evaluateValueTrigger(ep, j, state);
            } else {
                evaluateStateTrigger(ep, j, state);
            }
        }
    }

    xSemaphoreGive(configMutex_);
}

void WebhookManager::evaluateValueTrigger(WebhookEndpoint_t& ep, uint8_t trigIdx,
                                           const System_Global_State& state) {
    uint8_t epIdx = &ep - config_.endpoints;
    WebhookTrigger_t& trig = ep.triggers[trigIdx];
    float currentValue = getTriggerValue(trig.condition.source, state);
    float threshold = trig.condition.threshold;
    float hysteresis = getHysteresis(trig.condition.source);

    if (trig.condition.compare_op == WH_CMP_GT) {
        // 大于阈值触发，小于(阈值-迟滞)解除
        if (!trig.fired && currentValue > threshold) {
            trig.fired = true;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), I18n::get(STR_WH_VAL_GT),
                     trig.title, currentValue, threshold);
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_TRIGGER,
                                 trig.title, msg, trig.alert_level);
        } else if (trig.fired && currentValue < threshold - hysteresis) {
            trig.fired = false;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), I18n::get(STR_WH_VAL_RECOVER),
                     trig.title, currentValue);
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_RESOLVE,
                                 trig.title, msg, trig.alert_level);
        }
    } else if (trig.condition.compare_op == WH_CMP_LT) {
        // 小于阈值触发，大于(阈值+迟滞)解除
        if (!trig.fired && currentValue < threshold) {
            trig.fired = true;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), I18n::get(STR_WH_VAL_LT),
                     trig.title, currentValue, threshold);
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_TRIGGER,
                                 trig.title, msg, trig.alert_level);
        } else if (trig.fired && currentValue > threshold + hysteresis) {
            trig.fired = false;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), I18n::get(STR_WH_VAL_RECOVER),
                     trig.title, currentValue);
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_RESOLVE,
                                 trig.title, msg, trig.alert_level);
        }
    }
}

void WebhookManager::evaluateStateTrigger(WebhookEndpoint_t& ep, uint8_t trigIdx,
                                           const System_Global_State& state) {
    uint8_t epIdx = &ep - config_.endpoints;
    WebhookTrigger_t& trig = ep.triggers[trigIdx];
    uint8_t currentState = getTriggerState(trig.condition.source, state);

    if (trig.condition.compare_op == WH_CMP_EQ) {
        // 等于目标值触发，不等于时解除
        uint8_t targetState = (uint8_t)trig.condition.threshold;
        if (!trig.fired && currentState == targetState) {
            trig.fired = true;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), "%s: %s",
                     trig.title, getStateString(trig.condition.source, currentState));
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_TRIGGER,
                                 trig.title, msg, trig.alert_level);
        } else if (trig.fired && currentState != targetState) {
            trig.fired = false;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), I18n::get(STR_WH_STATE_RECOVER),
                     trig.title, getStateString(trig.condition.source, currentState));
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_RESOLVE,
                                 trig.title, msg, trig.alert_level);
        }
    } else if (trig.condition.compare_op == WH_CMP_NE) {
        // 不等于目标值触发（用于"任意故障"类），等于目标值时解除
        uint8_t targetState = (uint8_t)trig.condition.threshold;
        if (!trig.fired && currentState != targetState) {
            trig.fired = true;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), "%s: %s",
                     trig.title, getStateString(trig.condition.source, currentState));
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_TRIGGER,
                                 trig.title, msg, trig.alert_level);
        } else if (trig.fired && currentState == targetState) {
            trig.fired = false;
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), I18n::get(STR_WH_STATE_RECOVER),
                     trig.title, getStateString(trig.condition.source, currentState));
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_RESOLVE,
                                 trig.title, msg, trig.alert_level);
        }
    } else if (trig.condition.compare_op == WH_CMP_CHANGE) {
        // 任何状态变化都触发 (边缘检测，记录上一帧状态)
        bool valid = prevStateValid_[epIdx][trigIdx];
        uint8_t prev = prevState_[epIdx][trigIdx];
        if (valid && currentState != prev) {
            char msg[WH_DESC_MAX_LEN];
            snprintf(msg, sizeof(msg), "%s: %s -> %s",
                     trig.title, getStateString(trig.condition.source, prev),
                     getStateString(trig.condition.source, currentState));
            enqueueWebhookLocked(epIdx, trigIdx, WH_ACTION_TRIGGER,
                                 trig.title, msg, trig.alert_level);
        }
        prevState_[epIdx][trigIdx] = currentState;
        prevStateValid_[epIdx][trigIdx] = true;
    }
}

// =============================================================================
// 值/状态获取
// =============================================================================

float WebhookManager::getTriggerValue(uint8_t source, const System_Global_State& state) {
    switch (source) {
        case WH_VALUE_TEMPERATURE:   return state.bms.temperature;
        case WH_VALUE_CURRENT:       return (float)state.bms.current;
        case WH_VALUE_VOLTAGE:       return (float)state.bms.voltage;
        case WH_VALUE_SOC:           return state.bms.soc;
        case WH_VALUE_SOH:           return state.bms.soh;
        case WH_VALUE_INPUT_VOLTAGE: return (float)state.power.input_voltage;
        case WH_VALUE_BOARD_TEMP:    return state.system.board_temperature;
        default:                     return 0.0f;
    }
}

uint8_t WebhookManager::getTriggerState(uint8_t source, const System_Global_State& state) {
    switch (source) {
        case WH_STATE_AC_POWER:   return state.power.ac_present ? 1 : 0;
        case WH_STATE_CHARGER:    return state.power.charger_enabled ? 1 : 0;
        case WH_STATE_BMS_FAULT:  return (uint8_t)state.bms.fault_type;
        case WH_STATE_POWER_FAULT: return (uint8_t)state.power.fault_type;
        case WH_STATE_FSM_STATE:  return currentFsmState_;
        case WH_STATE_POWER_MODE: return state.power_mode;
        case WH_STATE_EMERGENCY:  return state.emergency_shutdown ? 1 : 0;
        case WH_STATE_BALANCING:  return state.bms.balancing_active ? 1 : 0;
        default:                  return 0;
    }
}

float WebhookManager::getHysteresis(uint8_t source) {
    switch (source) {
        case WH_VALUE_TEMPERATURE:
        case WH_VALUE_BOARD_TEMP:   return WH_HYSTERESIS_TEMP;
        case WH_VALUE_CURRENT:      return (float)WH_HYSTERESIS_MA;
        case WH_VALUE_VOLTAGE:
        case WH_VALUE_INPUT_VOLTAGE: return (float)WH_HYSTERESIS_MV;
        case WH_VALUE_SOC:
        case WH_VALUE_SOH:          return WH_HYSTERESIS_PCT;
        default:                    return 0.0f;
    }
}

const char* WebhookManager::getLevelString(uint8_t level) {
    switch (level) {
        case WH_LEVEL_CRITICAL: return "critical";
        case WH_LEVEL_WARNING:  return "warning";
        case WH_LEVEL_INFO:
        default:                return "info";
    }
}

// BMS 故障枚举 → 本地化故障名
static const char* whBmsFaultString(uint8_t value) {
    switch (value) {
        case BMS_FAULT_OVER_VOLTAGE:    return I18n::get(STR_WH_F_BMS_OV);
        case BMS_FAULT_UNDER_VOLTAGE:   return I18n::get(STR_WH_F_BMS_UV);
        case BMS_FAULT_OVER_CURRENT:    return I18n::get(STR_WH_F_BMS_OC);
        case BMS_FAULT_SHORT_CIRCUIT:   return I18n::get(STR_WH_F_BMS_SC);
        case BMS_FAULT_OVER_TEMP:       return I18n::get(STR_WH_F_BMS_OT);
        case BMS_FAULT_CHIP_ERROR:      return I18n::get(STR_WH_F_BMS_CHIP);
        case BMS_FAULT_PASSIVE_SHUTDOWN:return I18n::get(STR_WH_F_BMS_PASSIVE);
        case BMS_FAULT_NONE:            return I18n::get(STR_WH_ST_BMS_NORMAL);
        default:                        return I18n::get(STR_WH_ST_BMS_FAULT);
    }
}

// 电源故障枚举 → 本地化故障名
static const char* whPowerFaultString(uint8_t value) {
    switch (value) {
        case POWER_FAULT_CHIP_ERROR:           return I18n::get(STR_WH_F_PWR_CHIP);
        case POWER_FAULT_OVER_CURRENT:         return I18n::get(STR_WH_F_PWR_OC);
        case POWER_FAULT_OVER_TEMPERATURE:     return I18n::get(STR_WH_F_PWR_OT);
        case POWER_FAULT_INPUT_OVERVOLTAGE:    return I18n::get(STR_WH_F_PWR_IN_OV);
        case POWER_FAULT_INPUT_UNDERVOLTAGE:   return I18n::get(STR_WH_F_PWR_IN_UV);
        case POWER_FAULT_BATTERY_OVERVOLTAGE:  return I18n::get(STR_WH_F_PWR_BAT_OV);
        case POWER_FAULT_BATTERY_UNDERVOLTAGE: return I18n::get(STR_WH_F_PWR_BAT_UV);
        case POWER_FAULT_SHORT_CIRCUIT:        return I18n::get(STR_WH_F_PWR_SC);
        case POWER_FAULT_CHARGE_TIMEOUT:       return I18n::get(STR_WH_F_PWR_TIMEOUT);
        case POWER_FAULT_I2C_COMMUNICATION:    return I18n::get(STR_WH_F_PWR_I2C);
        case POWER_FAULT_NONE:                 return I18n::get(STR_WH_ST_PWR_NORMAL);
        default:                               return I18n::get(STR_WH_ST_PWR_FAULT);
    }
}

const char* WebhookManager::getStateString(uint8_t source, uint8_t value) {
    switch (source) {
        case WH_STATE_AC_POWER:   return value ? I18n::get(STR_WH_ST_AC_ON) : I18n::get(STR_WH_ST_AC_OFF);
        case WH_STATE_CHARGER:    return value ? I18n::get(STR_WH_ST_CHG_ON) : I18n::get(STR_WH_ST_CHG_OFF);
        case WH_STATE_BMS_FAULT:  return whBmsFaultString(value);
        case WH_STATE_POWER_FAULT: return whPowerFaultString(value);
        case WH_STATE_FSM_STATE:
            switch (value) {
                case 0: return I18n::get(STR_WH_ST_FSM_INIT);
                case 1: return I18n::get(STR_WH_ST_FSM_NORMAL);
                case 2: return I18n::get(STR_WH_ST_FSM_WARNING);
                case 3: return I18n::get(STR_WH_ST_FSM_CRITICAL);
                default: return I18n::get(STR_WH_ST_UNKNOWN);
            }
        case WH_STATE_POWER_MODE:
            switch (value) {
                case 0: return I18n::get(STR_WH_ST_PM_AC);
                case 1: return I18n::get(STR_WH_ST_PM_BATTERY);
                case 2: return I18n::get(STR_WH_ST_PM_HYBRID);
                case 3: return I18n::get(STR_WH_ST_PM_CHARGING);
                default: return I18n::get(STR_WH_ST_UNKNOWN);
            }
        case WH_STATE_EMERGENCY:  return value ? I18n::get(STR_WH_ST_EMERGENCY) : I18n::get(STR_WH_ST_FSM_NORMAL);
        case WH_STATE_BALANCING:  return value ? I18n::get(STR_WH_ST_BALANCING) : I18n::get(STR_WH_ST_BAL_STOP);
        default:                  return I18n::get(STR_WH_ST_UNKNOWN);
    }
}

// =============================================================================
// 队列操作
// =============================================================================

bool WebhookManager::enqueueWebhookLocked(uint8_t epIdx, uint8_t trigIdx, uint8_t action,
                                          const char* title, const char* desc, uint8_t level) {
    if (!webhookQueue_) return false;
    if (epIdx >= WH_MAX_ENDPOINTS || trigIdx >= WH_MAX_TRIGGERS) return false;

    // 冷却检查 (仅对 trigger 动作，resolve 不受冷却限制)
    if (action == WH_ACTION_TRIGGER && isCoolingDown(epIdx)) {
        DBG.printf("[Webhook] 端点 %d 冷却中，跳过\n", epIdx);
        return false;
    }

    WebhookQueueItem_t item;
    memset(&item, 0, sizeof(item));
    item.endpoint_index = epIdx;
    item.trigger_index = trigIdx;
    item.action = action;
    item.alert_level = level;
    item.timestamp = millis();

    const WebhookEndpoint_t& ep = config_.endpoints[epIdx];
    const WebhookTrigger_t& trig = ep.triggers[trigIdx];

    strlcpy(item.dedup_key, trig.dedup_key, sizeof(item.dedup_key));
    strlcpy(item.title, title, sizeof(item.title));
    strlcpy(item.message, desc, sizeof(item.message));

    if (xQueueSend(webhookQueue_, &item, 0) != pdTRUE) {
        DBG.println(F("[Webhook] 队列已满，丢弃消息"));
        return false;
    }

    // 记录入队时刻，作为冷却判定的基准 (trigger 动作)
    if (action == WH_ACTION_TRIGGER) {
        stats_[epIdx].last_trigger_ms = millis();
    }

    return true;
}

bool WebhookManager::isCoolingDown(uint8_t epIdx) {
    if (epIdx >= WH_MAX_ENDPOINTS) return true;
    uint32_t now = millis();
    uint32_t elapsed = now - stats_[epIdx].last_trigger_ms;
    return elapsed < config_.endpoints[epIdx].cooldown_ms;
}

// =============================================================================
// 测试端点 (同步发送)
// =============================================================================

bool WebhookManager::sendTestNow(uint8_t index, String& responseMsg, int& httpCode) {
    httpCode = 0;
    if (index >= WH_MAX_ENDPOINTS) {
        responseMsg = I18n::get(STR_WH_INVALID_INDEX);
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        responseMsg = I18n::get(STR_WH_WIFI_DOWN);
        return false;
    }

    // 快照端点配置，检查是否启用
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    bool enabled = config_.endpoints[index].enabled;
    xSemaphoreGive(configMutex_);
    if (!enabled) {
        responseMsg = I18n::get(STR_WH_ENDPOINT_DISABLED);
        return false;
    }

    // 构造测试消息，同步发送
    WebhookQueueItem_t testItem;
    memset(&testItem, 0, sizeof(testItem));
    testItem.endpoint_index = index;
    testItem.trigger_index = 0;
    testItem.action = WH_ACTION_TEST;
    testItem.alert_level = WH_LEVEL_INFO;
    testItem.timestamp = millis();
    strlcpy(testItem.dedup_key, "TEST_ALERT", sizeof(testItem.dedup_key));
    strlcpy(testItem.title, I18n::get(STR_WH_TEST_TITLE), sizeof(testItem.title));
    strlcpy(testItem.message, I18n::get(STR_WH_TEST_MSG), sizeof(testItem.message));

    return sendWebhook(testItem, responseMsg, &httpCode);
}

// =============================================================================
// FreeRTOS 后台任务
// =============================================================================

void WebhookManager::webhookTaskFunc(void* pvParameters) {
    WebhookManager* self = static_cast<WebhookManager*>(pvParameters);
    WebhookQueueItem_t item;

    for (;;) {
        // 阻塞等待队列项，超时 1 秒
        if (xQueueReceive(self->webhookQueue_, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // 检查 WiFi
            if (WiFi.status() != WL_CONNECTED) {
                DBG.println(F("[Webhook] WiFi 未连接，丢弃消息"));
                continue;
            }

            // 发送 (含重试)
            String responseMsg;
            bool success = false;
            for (uint8_t retry = 0; retry <= WH_MAX_RETRIES; retry++) {
                if (retry > 0) {
                    // 指数退避: 1s, 2s, 4s
                    vTaskDelay(pdMS_TO_TICKS(1000UL << (retry - 1)));
                }
                success = self->sendWebhook(item, responseMsg);
                if (success) break;
            }

            // 更新统计
            uint8_t epIdx = item.endpoint_index;
            if (epIdx < WH_MAX_ENDPOINTS) {
                xSemaphoreTake(self->configMutex_, portMAX_DELAY);
                self->stats_[epIdx].last_sent_ms = millis();
                self->stats_[epIdx].last_success = success;
                if (item.action == WH_ACTION_TRIGGER || item.action == WH_ACTION_TEST) {
                    self->stats_[epIdx].total_sent++;
                } else {
                    self->stats_[epIdx].total_resolved++;
                }
                if (!success) {
                    self->stats_[epIdx].total_failed++;
                }
                xSemaphoreGive(self->configMutex_);

                if (!success) {
                    DBG.printf("[Webhook] 端点 %d 发送失败: %s\n", epIdx, responseMsg.c_str());
                }
            }
        }
    }
}

// =============================================================================
// HTTP 发送
// =============================================================================

bool WebhookManager::sendWebhook(const WebhookQueueItem_t& item, String& responseMsg, int* httpCodeOut) {
    if (item.endpoint_index >= WH_MAX_ENDPOINTS) return false;

    // 快照端点配置和遥测数据 (避免与主循环并发读写)
    WebhookEndpoint_t ep;
    WebhookTelemetry_t tel;
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    ep = config_.endpoints[item.endpoint_index];
    tel = telemetry_;
    xSemaphoreGive(configMutex_);

    HTTPClient http;
    WebhookSecureClient* secureClient = nullptr;

    // URL 标签替换: 支持全部变量, 值做 URL 编码 (GET 场景消息通过 URL 传递)
    String url = substituteVariables(ep.url, item, tel, ep.auth_token, ep.device_key, true);
    if (url.startsWith("https://")) {
        secureClient = new WebhookSecureClient();
        if (ep.verify_tls) {
            // 使用 ESP32 内置根证书库做完整证书链校验
            secureClient->useBuiltinCABundle();
        } else {
            // 用户显式选择跳过证书校验
            secureClient->setInsecure();
        }
        http.begin(*secureClient, url);
    } else {
        http.begin(url);
    }

    http.setTimeout(WH_HTTP_TIMEOUT);

    // 自定义认证 Header：支持 "Name: Value" 格式，{token}/{key} 标签替换
    if (ep.auth_header[0] != '\0') {
        String spec(ep.auth_header);
        int colon = spec.indexOf(':');
        String hname, hval;
        if (colon > 0) {
            hname = spec.substring(0, colon);
            hname.trim();
            hval = spec.substring(colon + 1);
            hval.trim();
        } else {
            hname = spec;
            hname.trim();
            hval = ep.auth_token;   // 未写 Value 时默认值为 token
        }
        hval.replace("{token}", ep.auth_token);
        hval.replace("{key}", ep.device_key);
        if (hname.length() > 0 && hval.length() > 0) {
            http.addHeader(hname, hval);
        }
    }

    int httpCode;
    if (ep.method == WH_METHOD_GET) {
        httpCode = http.GET();
    } else {
        // POST: JSON 体走消息模板
        String payload = buildPayload(item, ep, tel);
        http.addHeader("Content-Type", "application/json");
        httpCode = http.POST(payload);
    }
    responseMsg = http.getString();

    bool success = (httpCode >= 200 && httpCode < 300);
    if (httpCodeOut) *httpCodeOut = httpCode;

    http.end();
    if (secureClient) delete secureClient;

    return success;
}

// =============================================================================
// 通用 Payload 构建
// =============================================================================

String WebhookManager::buildPayload(const WebhookQueueItem_t& item,
                                    const WebhookEndpoint_t& ep,
                                    const WebhookTelemetry_t& tel) {
    // 通用 Webhook: POST JSON，token/key 通过 URL / Header / 消息模板的 {token}/{key} 标签发送
    StaticJsonDocument<512> doc;

    doc["title"] = item.title;

    if (ep.message_template[0] != '\0') {
        doc["message"] = substituteVariables(ep.message_template, item, tel, ep.auth_token, ep.device_key);
    } else {
        doc["message"] = item.message;
    }

    doc["level"] = getLevelString(item.alert_level);
    doc["action"] = (item.action == WH_ACTION_RESOLVE) ? "resolve" : "trigger";
    doc["dedup_key"] = item.dedup_key;
    doc["device"] = WiFi.getHostname();
    doc["timestamp"] = item.timestamp;

    String output;
    serializeJson(doc, output);
    return output;
}

// =============================================================================
// 模板变量替换
// =============================================================================

// 对单个替换值做 URL 编码 (保留 URL 安全字符，其余按 UTF-8 字节逐个转义)
static String whUrlEncode(const String& in) {
    String out;
    out.reserve(in.length() * 3);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            out += buf;
        }
    }
    return out;
}

String WebhookManager::substituteVariables(const String& tpl,
                                            const WebhookQueueItem_t& item,
                                            const WebhookTelemetry_t& tel,
                                            const char* token, const char* key,
                                            bool urlEncode) {
    String result = tpl;

    auto enc = [urlEncode](const String& s) { return urlEncode ? whUrlEncode(s) : s; };
    auto encC = [&enc](const char* s) { return enc(String(s ? s : "")); };

    result.replace("{title}", encC(item.title));
    result.replace("{description}", encC(item.message));
    result.replace("{level}", enc(getLevelString(item.alert_level)));
    result.replace("{device}", enc(WiFi.getHostname()));
    result.replace("{dedup_key}", encC(item.dedup_key));
    result.replace("{action}", enc((item.action == WH_ACTION_RESOLVE) ? "resolve" : "trigger"));
    result.replace("{timestamp}", enc(String(item.timestamp)));

    // 密钥标签 (插入到 payload 中): {token} → token, {key} → device key
    result.replace("{token}", encC(token));
    result.replace("{key}", encC(key));

    // 实时遥测变量
    result.replace("{soc}", enc(String(tel.soc, 1)));
    result.replace("{voltage}", enc(String(tel.voltage)));
    result.replace("{current}", enc(String(tel.current)));
    result.replace("{temperature}", enc(String(tel.temperature, 1)));
    result.replace("{ac}", enc(tel.ac_present ? I18n::get(STR_WH_AC_ON) : I18n::get(STR_WH_AC_OFF)));
    result.replace("{soh}", enc(String(tel.soh, 1)));

    return result;
}

// =============================================================================
// 统计查询
// =============================================================================

bool WebhookManager::getEndpointStats(uint8_t index, WebhookEndpointStats_t& out) const {
    if (index >= WH_MAX_ENDPOINTS) return false;
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    out = stats_[index];
    xSemaphoreGive(configMutex_);
    return true;
}
