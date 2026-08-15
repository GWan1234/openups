/*
 * System Management Module Implementation
 * 
 * Finite State Machine (FSM) implementation for system coordination.
 * Implements a clear state machine with safety-first design:
 * 1. Data Collection - Read all sensors first
 * 2. State Decision - Determine next state based on conditions
 * 3. State Transition - Execute state change with safety checks
 * 4. Action Execution - Issue mutually exclusive control commands
 */

#include "system_management.h"
#include "config_manager.h"
#include "data_structures.h"
#include "hardware_interface.h"
#include "i2c_interface.h"
#include "bms.h"
#include "power_management.h"
#include "web_server.h"
#include "webhook_manager.h"
#include "pins_config.h"
#include "ups_hid_service.h"
#include "mqtt_service.h"
#include "XiaomiSensorBridge.h"
#include "shtc3.h"
#include "utils.h"
#include "debug.h"
#include <WiFi.h>
#include <SPIFFS.h>

extern WebhookManager* webhookManager;
#include <math.h>
#include "time_utils.h"

// External reference to system config (defined in sketch_jan14a.ino)
extern Configuration* systemConfig;
extern bool g_is_new_board;

// SPIFFS 互斥锁（保护并发访问）
SemaphoreHandle_t g_spiffs_mutex = nullptr;

// =============================================================================
// Constructor
// =============================================================================

SystemManagement::SystemManagement(
    HardwareInterface& hw,
    BMS* battery,
    PowerManagement* pm,
    ConfigManager& cm,
    UPS_HID_Service* upsHid,
    MQTTService* mqtt,
    XiaomiSensorBridge* xiaomi,
    SHTC3* shtc
) : hardware(&hw)
    , bms(battery)
    , powerManagement(pm)
    , configManager(&cm)
    , upsHidService(upsHid)
    , mqttService(mqtt)
    , xiaomiBridge(xiaomi)
    , shtc3(shtc)
    , currentState(SYS_STATE_INIT)
    , previousState(SYS_STATE_INIT)
    , systemInitialized(false)
    , isShutdownRequested(false)
    , millisOverflowCount(0)
    , lastStateChangeTime(0)
    , lastStatusUpdateTime(0)
    , lastIndicatorUpdateTime(0)
    , lastMillisValue(0)
    , criticalRecoveryCounter_(0)
    , delayedStartTime_(0)
    , delayedStartDelay_(5000)  // 默认延迟 5 秒
    , delayedStartExecuted_(false)
    , lastPowerModeChangeTime(0)
    , lastRegisterCheckTime(0)
    , regMismatchCountBQ24780s(0)
    , regMismatchCountBQ76920(0)
    , bq24780sRegWarning(false)
    , bq76920RegWarning(false)
    , cachedCriticalCondition_(false)
    , cachedWarningCondition_(false)
    , emergency_discharge_disabled_(false)
    , discharge_buzzer_active_(false)
    , sc_analysis_task_handle_(nullptr)
    , last_sc_analysis_time_(0) {
    
    // 初始化全局状态黑板
    memset(&globalState, 0, sizeof(System_Global_State));
    
    // 如果 BMS 或 PowerManagement 为 nullptr，在全局状态中记录警告 (非故障)
   if (!bms) {
        DBG.println(F("SystemManagement: BMS is nullptr - will run in WARNING mode"));
        globalState.overall_status = 1;  // 警告状态，非故障
    }
   if (!powerManagement) {
        DBG.println(F("SystemManagement: PowerManagement is nullptr - will run in WARNING mode"));
        globalState.overall_status = 1;  // 警告状态，非故障
    }
}

// =============================================================================
// Destructor
// =============================================================================

SystemManagement::~SystemManagement() {
    // USB HID 服务由外部（setup）管理，不在这里释放
}

// =============================================================================
// Initialization - 简化：允许模块初始化失败，不阻止系统启动
// =============================================================================

bool SystemManagement::initialize() {
    DBG.println(F("=== System Management FSM Initializing ==="));
    
    lastStateChangeTime = millis();
    lastMillisValue = millis();
    millisOverflowCount = 0;
    criticalRecoveryCounter_ = 0;
    
    // 解析固件版本字符串 "SIG:<PROJECT_NAME>:VER:<VERSION>"
    // 示例："SIG:OPENUPS-ESP32S3:VER:1.0.2"
    Utils::parseFirmwareVersion(FIRMWARE_ID_TAG, 
                                globalState.system.firmware_version, sizeof(globalState.system.firmware_version),
                                globalState.system.hardware_version, sizeof(globalState.system.hardware_version));
    
    // 订阅配置变更事件
    EventBus::getInstance().subscribe(EVT_CONFIG_SYSTEM_CHANGED, onConfigSystemChanged);
    EventBus::getInstance().subscribe(EVT_CONFIG_BMS_CHANGED, onConfigBmsChanged);
    EventBus::getInstance().subscribe(EVT_CONFIG_POWER_CHANGED, onConfigPowerChanged);
    
    // 订阅按键长按事件 - 重置 WiFi 配置
    EventBus::getInstance().subscribe(EVT_BTN_LONG_PRESS, onBtnLongPress);
    
    // 订阅 BMS 故障检测事件
    EventBus::getInstance().subscribe(EVT_BMS_FAULT_DETECTED, onBmsFaultDetected);
    
    // 订阅 BMS 运输模式请求事件
    EventBus::getInstance().subscribe(EVT_BMS_SHIPMODE_REQUEST, onBmsShipModeRequest);

    // 订阅 BMS 重置电池数据请求事件
    EventBus::getInstance().subscribe(EVT_BMS_RESET_BATTERY_DATA, onBmsResetBatteryData);

    // 订阅充电事件
    EventBus::getInstance().subscribe(EVT_PM_CHARGE_STARTED, onChargeStarted);
    EventBus::getInstance().subscribe(EVT_PM_CHARGE_COMPLETE, onChargeComplete);

    // 订阅均衡事件
    EventBus::getInstance().subscribe(EVT_BMS_BALANCING_STARTED, onBalancingStarted);
    EventBus::getInstance().subscribe(EVT_BMS_BALANCING_STOPPED, onBalancingStopped);

    hardware->syncInitialState();
    
    // 初始状态转换：从 INIT 状态开始，由 handleStateInit 决定下一步
    transitionToState(SYS_STATE_INIT);
    
    systemInitialized = true;
    hardware->setBuzzer(BUZZER_MODE_BEEP_ONCE);

    // 创建 SPIFFS 互斥锁
    if (!g_spiffs_mutex) {
        g_spiffs_mutex = xSemaphoreCreateMutex();
    }

    // 启动自消耗分析任务（低优先级，5分钟后首次执行，之后每24小时）
    // 栈 8192：SPIFFS 文件操作峰值 ~3KB，留足余量
    xTaskCreatePinnedToCore(
        scAnalysisTask,
        "sc_analysis",
        8192,
        this,
        1,          // 低优先级
        &sc_analysis_task_handle_,
        0           // 核心0（主循环通常在核心1）
    );

    DBG.println(F("=== System Management FSM Ready ==="));
    return true;
}

// =============================================================================
// FSM 主循环 - 四步流程（重构为明确状态机）
// =============================================================================

void SystemManagement::update() {
    if (!systemInitialized) {
        return;
    }
    
    // =========================================
    // [优先] 处理所有硬件中断和事件，确保数据是最新的
    // =========================================
    if (hardware) {
        hardware->update();
    }
    
    // =========================================
    // 新增：检查并执行延迟启动
    // =========================================
    checkAndExecuteDelayedStart();
    
    // =========================================
    // Step 1: 数据收集 - 读取所有传感器数据
    // =========================================
    collectData();
    
    // =========================================
    // Step 2: 状态决策 - 基于更新后的状态决定下一状态
    // =========================================
    SystemState nextState = decideNextState();
    
    // =========================================
    // Step 3: 状态流转 - 执行状态切换
    // =========================================
    performStateTransition(nextState);
    
    // =========================================
    // Step 4: 动作执行 - 发出互斥的控制指令
    // =========================================
    executeStateActions();

    // 状态循环动作（每个周期执行）
    onStateLoop();

}

// =============================================================================
// Step 1: 数据收集 - 分层采集策略
// =============================================================================

void SystemManagement::collectData() {
    // 更新系统运行时间（正确处理 millis() 溢出，使用秒为单位）
    unsigned long current_time = millis();
    
    // 检测 millis() 溢出：如果当前值小于上一次值，说明发生了溢出
    if (current_time < lastMillisValue) {
        millisOverflowCount++;
    }
    lastMillisValue = current_time;
    
    // 计算运行时间（秒）
    // 每次溢出 = 2^32 ms = 4294967 秒（约49.7天）
    // uptime = (溢出次数 * 4294967) + (当前millis / 1000)
    globalState.system.uptime = (millisOverflowCount * 4294967UL) + (current_time / 1000);

    // 检查保护标志（快速响应故障）
    globalState.over_current_protection = (globalState.power.fault_type != POWER_FAULT_NONE);

    // Step 1: 更新 BMS 数据（如果 BMS 存在）
    if (bms != nullptr) {
        // 检查是否需要更新慢速数据
        bms->update(globalState);
        // 同步自消耗计算结果到全局状态
        globalState.self_consumption_mA = bms->getSelfConsumption_mA();
        globalState.sc_segment_count = bms->getSCSegmentCount();
        globalState.sc_total_segments = bms->getSCTotalSegments();
        globalState.sc_last_update = bms->getSCLastUpdate();
        globalState.sc_last_check = bms->getSCLastCheck();

        // 同步内阻估算结果到全局状态
        const float* ir = bms->getInternalResistance();
        for (uint8_t i = 0; i < 5; i++) {
            globalState.bms.cell_internal_resistance[i] = ir[i];
        }
        globalState.bms.ir_sample_count = bms->getIRSampleCount();
    } else {
        // BMS 不存在时，标记为未连接
        globalState.bms.is_connected = false;
        globalState.bms.soc = 0;
        globalState.bms.voltage = 0;
        globalState.bms.current = 0;
    }
    
    // Step 2: 更新 PowerManagement 数据（如果存在，且依赖 BMS 数据）
    if (powerManagement != nullptr) {
        // PowerManagement 可以每循环更新，因为它需要快速响应 AC 状态
        powerManagement->update(globalState);
    } else {
        // PowerManagement 不存在时，标记基本状态
        globalState.power.ac_present = false;
    }

    // Step 3: 更新 SHTC3 温湿度传感器（如果存在）
    if (shtc3) {
        shtc3->update();
        globalState.system.board_temperature_sht = shtc3->getTemperature();
        globalState.system.board_humidity = shtc3->getHumidity();
    }

    // 每 2 秒更新一次 WiFi 状态（减少系统调用开销）
    if (current_time - lastSlowDataUpdate >= 2000) {
        globalState.system.wifi_connected = WiFi.status() == WL_CONNECTED;
        if (globalState.system.wifi_connected) {
            globalState.system.wifi_rssi = WiFi.RSSI();
            strncpy(globalState.system.wifi_ssid, WiFi.SSID().c_str(), 
                    sizeof(globalState.system.wifi_ssid) - 1);
        }
        lastSlowDataUpdate = current_time;
    }
    
    // 更新电源模式（带防抖）
    updatePowerMode();

    // 寄存器一致性检查，每 60 秒执行一次
    if (current_time - lastRegisterCheckTime >= 60000) {
        lastRegisterCheckTime = current_time;
        checkBQ24780sRegisters();
        checkBQ76920Registers();
    }
}

// =============================================================================
// Step 2: 状态决策 - 基于传感器数据和安全优先级 (简化版)
// =============================================================================

SystemManagement::SystemState SystemManagement::decideNextState() {
    // 缓存条件检查结果，供 handleState* 复用
    cachedCriticalCondition_ = checkCriticalConditions();
    if (cachedCriticalCondition_) {
        return SYS_STATE_CRITICAL;
    }

    cachedWarningCondition_ = checkWarningConditions();
    if (cachedWarningCondition_) {
        return SYS_STATE_WARNING;
    }

    return SYS_STATE_NORMAL;
}

// =============================================================================
// Step 3: 状态流转
// =============================================================================

void SystemManagement::performStateTransition(SystemState nextState) {
    if (nextState != currentState) {
        // 防抖检查
        if (millis() - lastStateChangeTime < STATE_DEBOUNCE_TIME) {
            return;
        }
        
        SystemState oldState = currentState;
        
        // 退出旧状态
        onStateExit(oldState);
        
        // 转换到新状态
        currentState = nextState;
        lastStateChangeTime = millis();
        
        // 进入新状态
        onStateEnter(currentState);
        
        // 同步 overall_status 到全局状态
        syncOverallStatus();
        
    }
}

// =============================================================================
// Step 4: 动作执行 - 精简版（仅数据采集，不执行控制）
// =============================================================================

void SystemManagement::executeStateActions() {
    //还是要发布数据的，比如说这个，比如说未来的mqtt。
    // 更新UPS HID服务
    if (upsHidService != nullptr) {
        upsHidService->update(globalState);
    }

    // MQTT loop
    if (mqttService) mqttService->loop(globalState);

    // 小米传感器桥接更新
    if (xiaomiBridge != nullptr) {
        xiaomiBridge->update(globalState);
    }

    // Webhook 触发器求值
    if (webhookManager != nullptr) {
        webhookManager->setFsmState((uint8_t)currentState);
        webhookManager->evaluateTriggers(globalState);
    }
}

// =============================================================================
// 状态生命周期回调
// =============================================================================

void SystemManagement::onStateEnter(SystemState newState) {
    DBG.printf_P(PSTR("FSM: Entering state: %s\n"),
                   newState == SYS_STATE_INIT ? "INIT" :
                   newState == SYS_STATE_NORMAL ? "NORMAL" :
                   newState == SYS_STATE_WARNING ? "WARNING" : "CRITICAL");

    // 进入新状态时重置恢复计数器
    if (newState != SYS_STATE_CRITICAL) {
        criticalRecoveryCounter_ = 0;
    }

    // 状态切换时输出提示信息到 Web 页面
    if (newState == SYS_STATE_CRITICAL) {
        if (!globalState.bms.is_connected) {
            addTip(STR_TIP_BMS_OFFLINE);
        } else if (!globalState.power.bq24780s_connected) {
            addTip(STR_TIP_BQ24780S_OFFLINE);
        } else if (globalState.bms.fault_type != BMS_FAULT_NONE) {
            addTip(STR_TIP_BMS_FAULT, static_cast<int>(globalState.bms.fault_type));
        } else {
            addTip(STR_TIP_SYSTEM_CRITICAL);
        }
    } else if (newState == SYS_STATE_WARNING) {
        if (globalState.power.fault_type != POWER_FAULT_NONE) {
            addTip(STR_TIP_POWER_FAULT, static_cast<int>(globalState.power.fault_type));
        } else if (globalState.over_current_protection) {
            addTip(STR_TIP_OVER_CURRENT);
        } else if (globalState.over_temp_protection) {
            addTip(STR_TIP_OVER_TEMP);
        } else if (bq24780sRegWarning) {
            addTip(STR_TIP_BQ24780S_REG_MISMATCH);
        } else if (bq76920RegWarning) {
            addTip(STR_TIP_BQ76920_REG_MISMATCH);
        } else {
            addTip(STR_TIP_SYSTEM_WARNING);
        }
    }

    // 状态切换时设置蜂鸣器（不在循环中重复设置，避免打断状态机）
    switch (newState) {
        case SYS_STATE_NORMAL:
            hardware->setBuzzer(BUZZER_MODE_OFF);
            break;
        case SYS_STATE_WARNING:
            hardware->setBuzzer(BUZZER_MODE_WARNING_BEEP);
            break;
        case SYS_STATE_CRITICAL:
            hardware->setBuzzer(BUZZER_MODE_ALARM);
            break;
        default:
            break;
    }
}

void SystemManagement::onStateExit(SystemState oldState) {
    // 清除旧状态的指示
    hardware->setBuzzer(BUZZER_MODE_OFF);
}

void SystemManagement::onStateLoop() {
    // 根据当前状态调用对应的状态处理函数
    switch (currentState) {
        case SYS_STATE_INIT:
            handleStateInit();
            break;
        case SYS_STATE_NORMAL:
            handleStateNormal();
            break;
        case SYS_STATE_WARNING:
            handleStateWarning();
            break;
        case SYS_STATE_CRITICAL:
            handleStateCritical();
            break;
    }

    // 紧急关机放电保护检查（INIT 状态之外均可运行）
    if (currentState != SYS_STATE_INIT) {
        checkEmergencyShutdown();
    }

    // 更新放电指示灯（INIT 状态之外均可运行）
    if (currentState != SYS_STATE_INIT) {
        updateDischargeIndicator();
    }
}




// =============================================================================
// 事件处理回调
// =============================================================================

void SystemManagement::onBtnLongPress(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        // 重置 WiFi 配置
        if (systemManager->configManager != nullptr) {
            systemManager->hardware->setBuzzer(BUZZER_MODE_BEEP_ONCE);
            systemManager->configManager->resetWiFiConfig();
        }
    }
}

// =============================================================================
// 辅助方法
// =============================================================================

void SystemManagement::transitionToState(SystemState newState) {
    if (currentState != newState) {
        SystemState oldState = currentState;
        onStateExit(oldState);
        currentState = newState;
        lastStateChangeTime = millis();
        onStateEnter(currentState);
    }
}

bool SystemManagement::isSystemReady() const {
   return systemInitialized;
}

SystemManagement::SystemState SystemManagement::getState() const {
    return currentState;
}

const char* SystemManagement::getStateString() const {
    switch (currentState) {
        case SYS_STATE_INIT: return "INIT";
        case SYS_STATE_NORMAL: return "NORMAL";
        case SYS_STATE_WARNING: return "WARNING";
        case SYS_STATE_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}


// =============================================================================
// FSM 状态处理函数 - 每个状态独立的处理逻辑
// =============================================================================

/**
 * @brief 处理 INIT 状态：初始化检查
 * 若设备就绪，转入 NORMAL；若设备缺失，转入 CRITICAL
 */
void SystemManagement::handleStateInit() {
    // 检查关键模块是否存在
    bool bms_ready = (bms != nullptr);
    bool pm_ready = (powerManagement != nullptr);
    
    if (bms_ready && pm_ready) {
        // 设备就绪，转入 NORMAL
        DBG.println(F("[FSM] INIT: All modules ready, transitioning to NORMAL"));
        transitionToState(SYS_STATE_NORMAL);
    } else {
        // 设备缺失，转入 CRITICAL
        DBG.println(F("[FSM] INIT: Missing modules, transitioning to CRITICAL"));
        if (!bms_ready) DBG.println(F("[FSM] INIT: BMS is nullptr"));
        if (!pm_ready) DBG.println(F("[FSM] INIT: PowerManagement is nullptr"));
        transitionToState(SYS_STATE_CRITICAL);
    }
}

/**
 * @brief 处理 NORMAL 状态：正常运行
 * 检测是否满足进入 WARNING 或 CRITICAL 的条件
 * 若保持正常，调用 applyIndicatorNormal() 执行呼吸绿灯
 */
void SystemManagement::handleStateNormal() {
    // 优先检测 CRITICAL 条件（模块掉线）
    if (cachedCriticalCondition_) {
        DBG.println(F("[FSM] NORMAL: CRITICAL condition detected"));
        transitionToState(SYS_STATE_CRITICAL);
        return;
    }

    // 检测 WARNING 条件（模块在线但存在故障）
    if (cachedWarningCondition_) {
        DBG.println(F("[FSM] NORMAL: WARNING condition detected"));
        transitionToState(SYS_STATE_WARNING);
        return;
    }

    // 保持正常状态，执行呼吸绿灯
    applyIndicatorNormal();
}

/**
 * @brief 处理 WARNING 状态：警告
 * 优先检测是否降级为 CRITICAL
 * 若故障恢复，转回 NORMAL
 * 若保持警告，调用 applyIndicatorWarning() 执行呼吸橙灯
 */
void SystemManagement::handleStateWarning() {
    // 优先检测 CRITICAL 条件（模块掉线/BMS全部错误）
    if (cachedCriticalCondition_) {
        DBG.println(F("[FSM] WARNING: CRITICAL condition detected, downgrading"));
        transitionToState(SYS_STATE_CRITICAL);
        return;
    }

    // 检测故障是否恢复
    if (!cachedWarningCondition_) {
        DBG.println(F("[FSM] WARNING: Conditions recovered, transitioning to NORMAL"));
        transitionToState(SYS_STATE_NORMAL);
        return;
    }

    // 保持警告状态，执行呼吸橙灯
    applyIndicatorWarning();
}

/**
 * @brief 处理 CRITICAL 状态：危急
 * 实现恢复防抖：只有当设备连续 N 次循环检测都正常且无故障时，才允许转回 NORMAL
 * 若保持危急，调用 applyIndicatorCritical() 执行呼吸红灯+蜂鸣器间歇响
 */
void SystemManagement::handleStateCritical() {
    // 检查是否满足恢复条件（使用缓存值）
    if (!cachedCriticalCondition_ && !cachedWarningCondition_) {
        // 条件恢复，增加计数器
        criticalRecoveryCounter_++;
        DBG.printf_P(PSTR("[FSM] CRITICAL: Recovery counter: %d/%d\n"),
                       criticalRecoveryCounter_, CRITICAL_RECOVERY_COUNT);

        // 连续 N 次检测正常，才允许恢复
        if (criticalRecoveryCounter_ >= CRITICAL_RECOVERY_COUNT) {
            DBG.println(F("[FSM] CRITICAL: Recovery confirmed, transitioning to NORMAL"));
            criticalRecoveryCounter_ = 0;
            transitionToState(SYS_STATE_NORMAL);
            return;
        }
    } else {
        // 条件未恢复，重置计数器
        if (criticalRecoveryCounter_ > 0) {
            DBG.println(F("[FSM] CRITICAL: Recovery counter reset due to ongoing issues"));
            criticalRecoveryCounter_ = 0;
        }
    }

    // 保持危急状态，执行呼吸红灯+蜂鸣器间歇响
    applyIndicatorCritical();
}

// =============================================================================
// 条件检查函数 - 提取复杂的布尔逻辑
// =============================================================================

/**
 * @brief 检查是否满足 CRITICAL 条件（模块掉线/不存在/BMS全部错误）
 * @return true 如果存在 CRITICAL 条件
 */
bool SystemManagement::checkCriticalConditions() const {
    // BMS 模块不存在（指针为空）
    if (bms == nullptr) {
        return true;
    }
    
    // PowerManagement 模块不存在（指针为空）
    if (powerManagement == nullptr) {
        return true;
    }
    
    // BMS 模块未连接（is_connected == false）
    if (!globalState.bms.is_connected) {
        return true;
    }
    
    // PowerManagement 模块未连接（bq24780s_connected == false）
    if (!globalState.power.bq24780s_connected) {
        return true;
    }
    
    // BMS 全部错误视为 CRITICAL
    // 包括：过压、欠压、过流、短路、过温、芯片错误、被动关机
    if (globalState.bms.fault_type != BMS_FAULT_NONE) {
        return true;
    }
    
    return false;
}

/**
 * @brief 检查是否满足 WARNING 条件（模块在线但存在非BMS故障）
 * @return true 如果存在 WARNING 条件
 */
bool SystemManagement::checkWarningConditions() const {
    // PowerManagement 故障（fault_type != NONE）
    if (powerManagement && globalState.power.fault_type != POWER_FAULT_NONE) {
        return true;
    }
    
    // 硬件保护标志
    if (globalState.over_current_protection || globalState.over_temp_protection) {
        return true;
    }

    // 芯片保护配置是否一致

    if (bq24780sRegWarning || bq76920RegWarning) {
        return true;
    }
    
    return false;
}

// =============================================================================
// 指示灯控制函数 - 纯动作执行，不含业务逻辑
// =============================================================================

/**
 * @brief 设置正常指示：绿呼吸，关蜂鸣
 */
void SystemManagement::applyIndicatorNormal() {
    hardware->setRGBLED(RGB_MODE_BREATHING, {0, 255, 0});
    hardware->setLED(POWER_LED_PIN,LED_MODE_ON);
    // 蜂鸣器由 onStateEnter/onStateExit 控制，不在循环中设置
}

/**
 * @brief 设置警告指示：橙呼吸，关蜂鸣
 */
void SystemManagement::applyIndicatorWarning() {
    hardware->setRGBLED(RGB_MODE_BREATHING, {255, 165, 0});
    hardware->setLED(POWER_LED_PIN,LED_MODE_BLINK_SLOW);
    // 蜂鸣器由 onStateEnter/onStateExit 控制，不在循环中设置
}

/**
 * @brief 设置危急指示：红呼吸，开蜂鸣间歇
 */
void SystemManagement::applyIndicatorCritical() {
    hardware->setRGBLED(RGB_MODE_BREATHING, {255, 0, 0});
    hardware->setLED(POWER_LED_PIN,LED_MODE_BLINK_FAST);
    // 蜂鸣器由 onStateEnter/onStateExit 控制，不在循环中设置
}

/**
 * @brief 紧急关机放电保护检查
 * 条件：AC 离线 且 BMS SOC <= discharge_soc_stop * 150% 时，设置 emergency_shutdown = true
 *       SOC <= discharge_soc_stop 时，关闭 BMS 放电开关
 * 恢复：AC 在线后 emergency_shutdown = false
 *       BMS SOC >= discharge_soc_stop * 150% 时，打开 BMS 放电开关
 */
void SystemManagement::checkEmergencyShutdown() {
    if (bms == nullptr || powerManagement == nullptr || !globalState.bms.is_connected) {
        return;
    }

    // 获取放电停止 SOC 阈值
    Power_Config_t* powerConfig = configManager->getPowerConfig();
    float soc_stop = powerConfig->discharge_soc_stop;
    float soc_threshold = soc_stop * 1.5f;  // 150% 阈值
    float current_soc = globalState.bms.soc;
    bool ac_online = globalState.power.ac_present;

    if (!ac_online) {
        // AC 离线：检查是否需要紧急关机
        if (current_soc <= soc_threshold) {
            if (!globalState.emergency_shutdown) {
                globalState.emergency_shutdown = true;
                DBG.printf_P(PSTR("[EmergencyShutdown] AC offline + SOC %.1f%% <= %.1f%% (150%% of %.1f%%), emergency shutdown activated\n"),
                               current_soc, soc_threshold, soc_stop);
            }

            // SOC <= discharge_soc_stop 时，关闭 BMS 放电开关
            if (current_soc <= soc_stop && !emergency_discharge_disabled_) {
                DBG.printf_P(PSTR("[EmergencyShutdown] SOC %.1f%% <= %.1f%%, disabling BMS discharge\n"),
                               current_soc, soc_stop);
                bms->disableDischarge();
                emergency_discharge_disabled_ = true;
            }
        }
    } else {
        // AC 在线：恢复条件
        if (globalState.emergency_shutdown) {
            globalState.emergency_shutdown = false;
            DBG.println(F("[EmergencyShutdown] AC online, emergency shutdown cleared"));
        }

        // SOC >= discharge_soc_stop * 150% 时，重新打开 BMS 放电开关
        if (emergency_discharge_disabled_ && current_soc >= soc_threshold) {
            DBG.printf_P(PSTR("[EmergencyShutdown] AC online + SOC %.1f%% >= %.1f%%, re-enabling BMS discharge\n"),
                           current_soc, soc_threshold);
            bms->enableDischarge();
            emergency_discharge_disabled_ = false;
        }
    }
}

/**
 * @brief 更新放电指示灯
 * 独立于主状态颜色逻辑，但在 INIT 状态之外均可运行
 * 逻辑：若 globalState.bms.current < -10 (mA)，亮 DISCHARGE_LED_PIN；否则灭
 * 
 * 电流阈值说明：
 * - 正值表示充电电流
 * - 负值表示放电电流
 * - 阈值 -10mA 用于过滤微小电流波动，避免指示灯频繁闪烁
 */
void SystemManagement::updateDischargeIndicator() {
    // 电流阈值防抖：仅在放电电流 > 10mA (即 current < -10) 时亮起
    bool is_discharging = globalState.bms.current < DISCHARGE_CURRENT_THRESHOLD;

    if (is_discharging) {
        hardware->setLED(DISCHARGING_LED_PIN, LED_MODE_ON);
    } else {
        hardware->setLED(DISCHARGING_LED_PIN, LED_MODE_OFF);
    }

    // 放电蜂鸣器：仅在 NORMAL 状态 + AC 离线时生效
    // 由 onStateLoop 统一控制蜂鸣器，避免多处调用 setBuzzer 互相干扰
    if (currentState == SYS_STATE_NORMAL && !globalState.power.ac_present) {
        if (is_discharging && !discharge_buzzer_active_) {
            hardware->setBuzzer(BUZZER_MODE_SLOW_BEEP);
            discharge_buzzer_active_ = true;
        } else if (!is_discharging && discharge_buzzer_active_) {
            hardware->setBuzzer(BUZZER_MODE_OFF);
            discharge_buzzer_active_ = false;
        }
    } else if (discharge_buzzer_active_) {
        // 离开 NORMAL 状态或 AC 恢复，清除放电蜂鸣器
        hardware->setBuzzer(BUZZER_MODE_OFF);
        discharge_buzzer_active_ = false;
    }
}

// =============================================================================
// 配置变更事件回调实现
// =============================================================================

/**
 * @brief 系统配置变更事件回调
 * @param type 事件类型
 * @param param 事件参数 (Configuration*)
 */
void SystemManagement::onConfigSystemChanged(EventType type, void* param) {
    
    if (param == nullptr) {
        DBG.println(F("[SysMgr] ERROR: Config param is null"));
        return;
    }
    
    Configuration* config = static_cast<Configuration*>(param);
    
    // 空指针检查
    if (systemManager == nullptr || systemManager->hardware == nullptr) {
        DBG.println(F("[SysMgr] ERROR: System manager or hardware is null"));
        return;
    }
    
    // 提取配置参数
    bool buzzer_enabled = config->buzzer_enabled;
    uint8_t buzzer_volume = config->buzzer_volume;
    uint8_t led_brightness = config->led_brightness;
    
    // 应用新配置到硬件接口
    systemManager->hardware->setBuzzerEnabled(buzzer_enabled);
    systemManager->hardware->setBuzzerVolume(buzzer_volume);
    systemManager->hardware->setLEDBrightness(led_brightness);

    DBG.println(F("[SysMgr] System config applied successfully"));
}

/**
 * @brief BMS 配置变更事件回调
 * @param type 事件类型
 * @param param 事件参数 (BMS_Config_t*)
 */
void SystemManagement::onConfigBmsChanged(EventType type, void* param) {

    if (param == nullptr) {
        DBG.println(F("[SysMgr] ERROR: BMS config param is null"));
        return;
    }
    
    BMS_Config_t* config = static_cast<BMS_Config_t*>(param);
    
    // 空指针检查
    if (systemManager == nullptr || systemManager->bms == nullptr) {
        DBG.println(F("[SysMgr] ERROR: System manager or BMS is null"));
        return;
    }
    
    // 调用 BMS 的 applyNewConfig 方法
    if (!systemManager->bms->applyNewConfig(*config)) {
        DBG.println(F("[SysMgr] ERROR: Failed to apply BMS config"));
        return;
    }
    
    DBG.println(F("[SysMgr] BMS config applied successfully"));
}

/**
 * @brief 电源配置变更事件回调
 * @param type 事件类型
 * @param param 事件参数 (Power_Config_t*)
 */
void SystemManagement::onConfigPowerChanged(EventType type, void* param) {
    if (param == nullptr) {
        DBG.println(F("[SysMgr] ERROR: Power config param is null"));
        return;
    }
    
    Power_Config_t* config = static_cast<Power_Config_t*>(param);
    
    // 空指针检查
    if (systemManager == nullptr || systemManager->powerManagement == nullptr) {
        DBG.println(F("[SysMgr] ERROR: System manager or PowerManagement is null"));
        return;
    }
    
    // 调用 PowerManagement 的 applyNewConfig 方法
    if (!systemManager->powerManagement->applyNewConfig(*config)) {
        DBG.println(F("[SysMgr] ERROR: Failed to apply Power config"));
        return;
    }
    
    DBG.println(F("[SysMgr] Power config applied successfully"));
}

// =============================================================================
// 延迟启动相关方法实现
// =============================================================================

/**
 * @brief 检查并执行延迟启动
 */
void SystemManagement::checkAndExecuteDelayedStart() {
    if (delayedStartExecuted_) {
        return;  // 已执行，直接返回
    }
    
    // 首次调用时记录基准时间
    if (delayedStartTime_ == 0) {
        delayedStartTime_ = millis();
        return;
    }
    
    // 检查是否达到延迟时间
    unsigned long elapsed = millis() - delayedStartTime_;
    if (elapsed >= delayedStartDelay_) {
        onDelayedStart();  // 调用延迟启动回调
        delayedStartExecuted_ = true;
    }
}

/**
 * @brief 同步 overall_status 到全局状态
 * 根据当前 FSM 状态更新 globalState.overall_status
 */
void SystemManagement::syncOverallStatus() {
    uint8_t new_status;
    
    switch (currentState) {
        case SYS_STATE_NORMAL:
            new_status = OVERALL_STATUS_NORMAL;
            break;
        case SYS_STATE_WARNING:
            new_status = OVERALL_STATUS_WARNING;
            break;
        case SYS_STATE_CRITICAL:
            new_status = OVERALL_STATUS_FAULT;
            break;
        default:  // INIT
            new_status = OVERALL_STATUS_NORMAL;
            break;
    }
    
    if (globalState.overall_status != new_status) {
        globalState.overall_status = new_status;
    }
}

/**
 * @brief 更新电源模式
 * 根据 AC 状态、充电状态、混合供电状态决定 power_mode
 * 带防抖逻辑（2秒），避免 AC 插拔瞬间模式抖动
 */
void SystemManagement::updatePowerMode() {
    uint8_t new_mode;
    
    if (globalState.power.ac_present) {
        // AC 存在，判断具体模式
        if (globalState.power.charger_enabled && 
            globalState.bms.current > CHARGING_CURRENT_THRESHOLD) {
            new_mode = POWER_MODE_CHARGING;      // 3: 充电中
        } else if (globalState.power.hybrid_mode) {
            new_mode = POWER_MODE_HYBRID;        // 2: 混合供电
        } else {
            new_mode = POWER_MODE_AC;            // 0: AC 供电
        }
    } else {
        // AC 不存在，纯电池
        new_mode = POWER_MODE_BATTERY;           // 1: 电池供电
    }
    
    // 防抖：仅在模式变化且超过防抖时间才更新
    if (globalState.power_mode != new_mode) {
        unsigned long current_time = millis();
        if (current_time - lastPowerModeChangeTime >= POWER_MODE_DEBOUNCE_TIME) {
            globalState.power_mode = new_mode;
            lastPowerModeChangeTime = current_time;
        }
    }
}

/**
 * @brief BMS 故障检测事件回调（静态方法，供 EventBus 调用）
 * @param type 事件类型
 * @param param 事件参数 (int* 故障码)
 */
void SystemManagement::onBmsFaultDetected(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        BMS_Fault_t fault_type;
        
        if (param != nullptr) {
            fault_type = static_cast<BMS_Fault_t>(*static_cast<int*>(param));
        } else {
            // 如果没有参数，从全局状态获取当前故障类型
            fault_type = systemManager->globalState.bms.fault_type;
        }
        systemManager->handleBmsFault(fault_type);
    }
}

/**
 * @brief BMS 故障处理（成员函数）
 * 根据 BMS 故障类型执行相应的保护措施
 * @param fault_type BMS 故障类型
 * 
 * 故障处理策略：
 * - BMS_FAULT_OVER_TEMP:     过温保护 - 关闭充放电
 * - BMS_FAULT_OVER_VOLTAGE:  过压保护 - 关闭充电
 * - BMS_FAULT_UNDER_VOLTAGE: 欠压保护 - 关闭放电
 * - BMS_FAULT_OVER_CURRENT:  过流保护 - 关闭充放电
 * - BMS_FAULT_SHORT_CIRCUIT: 短路保护 - 紧急关闭充放电
 * - BMS_FAULT_CHIP_ERROR:    芯片错误 - 不处理，因为已经无法操作了
 * - BMS_FAULT_PASSIVE_SHUTDOWN: 被动关机 - 关闭充放电
 */
void SystemManagement::handleBmsFault(BMS_Fault_t fault_type) {
    if (!bms || !powerManagement) {
        DBG.println(F("[SysMgr] Cannot handle BMS fault: BMS or PowerManagement is null"));
        return;
    }
    
    switch (fault_type) {
        case BMS_FAULT_OVER_TEMP:
            DBG.println(F("[SysMgr] OVER TEMP protection: Disabling both charge and discharge"));
            addTip(STR_TIP_BMS_OVER_TEMP);
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_OVER_VOLTAGE:
            DBG.println(F("[SysMgr] OVER VOLTAGE protection: Disabling charge"));
            addTip(STR_TIP_BMS_OVER_VOLTAGE);
            bms->disableCharge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_UNDER_VOLTAGE:
            DBG.println(F("[SysMgr] UNDER VOLTAGE protection: Disabling discharge"));
            addTip(STR_TIP_BMS_UNDER_VOLTAGE);
            bms->disableDischarge();
            break;
            
        case BMS_FAULT_OVER_CURRENT:
            DBG.println(F("[SysMgr] OVER CURRENT protection: Disabling both charge and discharge"));
            addTip(STR_TIP_BMS_OVER_CURRENT);
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_SHORT_CIRCUIT:
            DBG.println(F("[SysMgr] SHORT CIRCUIT protection: Emergency shutdown!"));
            addTip(STR_TIP_BMS_SHORT_CIRCUIT);
            bms->emergencyShutdown();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_CHIP_ERROR:
            DBG.println(F("[SysMgr] CHIP ERROR protection: Disabling both charge and discharge"));
            addTip(STR_TIP_BMS_CHIP_ERROR);
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_PASSIVE_SHUTDOWN:
            DBG.println(F("[SysMgr] PASSIVE SHUTDOWN protection: Disabling both charge and discharge"));
            addTip(STR_TIP_BMS_PASSIVE_SHUTDOWN);
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_NONE:
            DBG.println(F("[SysMgr] BMS fault cleared"));
            break;
            
        default:
            DBG.printf_P(PSTR("[SysMgr] Unknown BMS fault type: %d, taking safe action\n"), 
                           static_cast<int>(fault_type));
            addTip(STR_TIP_BMS_UNKNOWN_FAULT, static_cast<int>(fault_type));
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
    }
}

/**
 * @brief 延迟启动回调函数
 * 在系统运行指定时间后自动调用一次
 * 用户可以在此函数中实现自定义的延迟初始化逻辑
 */
void SystemManagement::onDelayedStart() {
    if (upsHidService != nullptr) {
        //延迟执行 ups 启动，保证数据获取正常
        upsHidService->begin();
    }

    // 小米传感器桥接延迟启动
    if (xiaomiBridge != nullptr) {
        xiaomiBridge->begin();
    }

    if (mqttService != nullptr && systemConfig != nullptr) {
        // 检查 MQTT 配置是否有效（broker 和端口）
        if (strlen(systemConfig->mqtt_broker) > 0 && systemConfig->mqtt_port > 0) {
            DBG.println("[SysMgr] Starting MQTT service...");
            mqttService->setBrokerAddress(systemConfig->mqtt_broker);
            mqttService->setBrokerPort(systemConfig->mqtt_port);
            if (strlen(systemConfig->mqtt_username) > 0) {
                mqttService->setBrokerCredentials(
                    systemConfig->mqtt_username,
                    systemConfig->mqtt_password);
            }
            mqttService->setDeviceIdentifier(systemConfig->identifier);
            bool begin_ok = mqttService->begin(systemConfig, &globalState);
            bool connect_ok = mqttService->connect();
            if (!connect_ok) {
                DBG.println("[SysMgr] WARNING: MQTT connect failed, will retry in loop()");
            }
        } else {
            DBG.println("[SysMgr] MQTT not configured, skipping");
        }
    } else {
        DBG.println("[SysMgr] MQTT service not created (config not enabled)");
    }

    // Webhook 延迟启动 (与 HID / MQTT / XiaomiBridge 一起，等系统稳定后再启动)
    if (webhookManager == nullptr) {
        webhookManager = new WebhookManager(*configManager);
        if (!webhookManager->begin()) {
            DBG.println("[SysMgr] WARNING: Webhook start failed");
            delete webhookManager;
            webhookManager = nullptr;
        }
    }
}

// =============================================================================
// 寄存器一致性检查函数实现
// =============================================================================

/**
 * @brief 检查 BQ24780S 寄存器值与配置是否一致
 * 连续 3 次不匹配才触发警告
 * 调用处负责控制检查频率
 */
void SystemManagement::checkBQ24780sRegisters() {
    if (!powerManagement || !configManager) return;
    if (!globalState.power.bq24780s_connected) {
        regMismatchCountBQ24780s = 0;
        bq24780sRegWarning = false;
        return;
    }

    Power_Config_t* config = configManager->getPowerConfig();
    uint16_t* regs = globalState.power.bq24780s_registers;

    // 寄存器数据一次性读取，任一为0x0000说明整批数据无效，跳过比对
    if (regs[9] == 0x0000 || regs[10] == 0x0000) {
        regMismatchCountBQ24780s = 0;
        bq24780sRegWarning = false;
        return;
    }

    bool mismatch = false;

    int reg_discharge = Utils::parseBQ24780sDischargeCurrent(regs[9]);
    if (reg_discharge > 0 && abs(reg_discharge - (int)config->max_discharge_current) > config->max_discharge_current * 0.05f) {
        mismatch = true;
        if (bq24780sRegWarning || regMismatchCountBQ24780s >= 2)
            addTip(STR_TIP_REG_MISMATCH_mA, reg_discharge, config->max_discharge_current);
    }

    int reg_input = Utils::parseBQ24780sInputCurrent(regs[10]);
    if (reg_input > 0 && abs(reg_input - (int)config->over_current_threshold) > config->over_current_threshold * 0.05f) {
        mismatch = true;
        if (bq24780sRegWarning || regMismatchCountBQ24780s >= 2)
            addTip(STR_TIP_REG_MISMATCH_mA, reg_input, config->over_current_threshold);
    }

    if (mismatch) {
        if (++regMismatchCountBQ24780s >= 3) bq24780sRegWarning = true;
    } else {
        regMismatchCountBQ24780s = 0;
        bq24780sRegWarning = false;
    }
}

/**
 * @brief 检查 BQ76920 寄存器值与配置是否一致
 * 连续 3 次不匹配才触发警告
 * 调用处负责控制检查频率
 */
void SystemManagement::checkBQ76920Registers() {
    if (!bms || !configManager) return;
    if (!globalState.bms.is_connected) {
        regMismatchCountBQ76920 = 0;
        bq76920RegWarning = false;
        return;
    }

    BMS_Config_t* config = configManager->getBMSConfig();
    uint8_t* regs = globalState.bms.bq76920_registers;

    // 寄存器数据一次性读取，任一为0x00说明整批数据无效，跳过比对
    if (regs[4] == 0x00 || regs[5] == 0x00 || regs[7] == 0x00 ||
        regs[8] == 0x00 || regs[10] == 0x00 || regs[11] == 0x00) {
        regMismatchCountBQ76920 = 0;
        bq76920RegWarning = false;
        return;
    }

    bool mismatch = false;

    int reg_scd = Utils::parseBQ76920Protect1(regs[4]);
    if (reg_scd > 0 && config->short_circuit_threshold > 0 && abs(reg_scd - (int)config->short_circuit_threshold) > config->short_circuit_threshold * 0.15f) {
        mismatch = true;
        if (bq76920RegWarning || regMismatchCountBQ76920 >= 2)
            addTip(STR_TIP_REG_MISMATCH_mA, reg_scd, config->short_circuit_threshold);
    }

    int reg_ocd = Utils::parseBQ76920Protect2(regs[5], regs[4]);
    if (reg_ocd > 0 && config->max_discharge_current > 0 && abs(reg_ocd - (int)config->max_discharge_current) > config->max_discharge_current * 0.15f) {
        mismatch = true;
        if (bq76920RegWarning || regMismatchCountBQ76920 >= 2)
            addTip(STR_TIP_REG_MISMATCH_mA, reg_ocd, config->max_discharge_current);
    }

    float reg_ov = Utils::parseBQ76920OvTrip(regs[7], regs[10], regs[11]);
    if (reg_ov > 0 && config->cell_ov_threshold > 0 && fabs(reg_ov - config->cell_ov_threshold) > config->cell_ov_threshold * 0.02f) {
        mismatch = true;
        if (bq76920RegWarning || regMismatchCountBQ76920 >= 2)
            addTip(STR_TIP_REG_MISMATCH_mV, (int)reg_ov, config->cell_ov_threshold);
    }

    float reg_uv = Utils::parseBQ76920UvTrip(regs[8], regs[10], regs[11]);
    if (reg_uv > 0 && config->cell_uv_threshold > 0 && fabs(reg_uv - config->cell_uv_threshold) > config->cell_uv_threshold * 0.02f) {
        mismatch = true;
        if (bq76920RegWarning || regMismatchCountBQ76920 >= 2)
            addTip(STR_TIP_REG_MISMATCH_mV, (int)reg_uv, config->cell_uv_threshold);
    }

    if (mismatch) {
        if (++regMismatchCountBQ76920 >= 3) bq76920RegWarning = true;
    } else {
        regMismatchCountBQ76920 = 0;
        bq76920RegWarning = false;
    }
}

// =============================================================================
// BMS 运输模式请求事件回调实现
// =============================================================================

/**
 * @brief BMS 运输模式请求事件回调（静态方法，供 EventBus 调用）
 * @param type 事件类型
 * @param param 事件参数 (nullptr)
 */
void SystemManagement::onBmsShipModeRequest(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        DBG.println(F("[SysMgr] BMS Ship Mode Request event received"));
        systemManager->handleBmsShipModeRequest();
    }
}

/**
 * @brief BMS 运输模式处理（成员函数）
 * 执行 BMS 的 enterShipMode 指令，并将 BMS 标记为离线
 */
void SystemManagement::handleBmsShipModeRequest() {
    if (!bms) {
        DBG.println(F("[SysMgr] ERROR: Cannot enter ship mode - BMS is null"));
        return;
    }
    
    DBG.println(F("[SysMgr] Executing BMS enterShipMode command..."));
    
    // 执行 BMS 的 enterShipMode 指令
    bool result = bms->enterShipMode();
    
    if (result) {
        DBG.println(F("[SysMgr] BMS entered ship mode successfully"));
        // 将 BMS 标记为离线
        globalState.bms.is_connected = false;
        DBG.println(F("[SysMgr] BMS marked as offline"));
    } else {
        DBG.println(F("[SysMgr] ERROR: Failed to enter BMS ship mode"));
    }
}

/**
 * @brief BMS 重置电池数据请求事件回调（静态方法，供 EventBus 调用）
 */
void SystemManagement::onBmsResetBatteryData(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        DBG.println(F("[SysMgr] BMS Reset Battery Data event received"));
        systemManager->handleBmsResetBatteryData();
    }
}

/**
 * @brief BMS 重置电池数据处理（成员函数）
 * 重置 SOH、循环次数、均衡统计等电池生命周期数据
 */
void SystemManagement::handleBmsResetBatteryData() {
    if (!bms) {
        DBG.println(F("[SysMgr] ERROR: Cannot reset battery data - BMS is null"));
        return;
    }

    DBG.println(F("[SysMgr] Executing BMS resetBatteryData..."));

    bool result = bms->resetBatteryData();

    if (result) {
        // 重置全局状态中的BMS数据
        globalState.bms.soh = 100.0f;
        globalState.bms.cycle_count = 0;
        globalState.bms.balancing_events_total = 0;
        for (int i = 0; i < 5; i++) {
            globalState.bms.cell_balancing_count[i] = 0;
        }
        addTip(STR_TIP_BATTERY_RESET);
        DBG.println(F("[SysMgr] BMS battery data reset successfully"));
    } else {
        DBG.println(F("[SysMgr] ERROR: Failed to reset BMS battery data"));
    }
}

void SystemManagement::onChargeStarted(EventType type, void* param) {
    if (!systemManager || !systemManager->systemInitialized) return;
    uint32_t info = param ? *static_cast<uint32_t*>(param) : 0;
    uint16_t current = info & 0xFFFF;
    uint16_t voltage = (info >> 16) & 0xFFFF;
    systemManager->addTip(STR_TIP_CHARGING,
                          systemManager->globalState.bms.soc, current, voltage);
}

void SystemManagement::onChargeComplete(EventType type, void* param) {
    if (!systemManager || !systemManager->systemInitialized) return;
    auto& st = systemManager->globalState;
    systemManager->addTip(STR_TIP_CHARGE_STOPPED, st.bms.soc);
}

void SystemManagement::onBalancingStarted(EventType type, void* param) {
    if (!systemManager || !systemManager->systemInitialized) return;
    uint8_t mask = param ? *static_cast<uint8_t*>(param) : 0;
    char cells[32] = {0};
    int pos = 0;
    for (int i = 0; i < 5; i++) {
        if (mask & (1 << i)) {
            if (pos > 0) cells[pos++] = ',';
            pos += snprintf(cells + pos, sizeof(cells) - pos, "%d", i + 1);
        }
    }
    systemManager->addTip(STR_TIP_BALANCE_START, systemManager->globalState.bms.soc, cells);
}

void SystemManagement::onBalancingStopped(EventType type, void* param) {
    if (!systemManager || !systemManager->systemInitialized) return;
    systemManager->addTip(STR_TIP_BALANCE_STOP, systemManager->globalState.bms.soc);
}

// =============================================================================
// ADC Calibration 访问函数实现
// =============================================================================

/**
 * @brief 获取 ADC 校准系数数组
 * @return 校准系数数组指针，如果 hardware 为 nullptr 则返回 nullptr
 */
const uint8_t* SystemManagement::getADCCalibration() {
    return hardware ? hardware->getADCCalibration() : nullptr;
}

/**
 * @brief 设置单个 ADC 通道的校准系数
 * @param pin ADC 引脚编号
 * @param coefficient 校准系数
 */
void SystemManagement::setADCCalibration(uint8_t pin, uint8_t coefficient) {
    if (hardware) {
        hardware->setADCCalibration(pin, coefficient);
    }
}

// =============================================================================
// 持久化所有模块数据
// =============================================================================

void SystemManagement::saveAllData() {
    DBG.println(F("[SysMgr] Saving all module data..."));
    if (bms) {
        bms->saveToStorage();
    }
    if (configManager) {
        configManager->saveConfiguration();
    }
    DBG.println(F("[SysMgr] All module data saved"));
}

void SystemManagement::addTip(StrId fmtId, ...) {
    if (fmtId >= STR_COUNT) return;

    char buf[SYSTEM_TIP_MAX_LEN];
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);

    const char* timeFmt = I18n::get(STR_TIP_TIME_FMT);
    int off = snprintf(buf, sizeof(buf), timeFmt,
                       t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);

    const char* fmt = I18n::get(fmtId);
    va_list args;
    va_start(args, fmtId);
    vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);

    uint8_t idx = globalState.tip_index;
    globalState.tips[idx].timestamp = (uint32_t)now;
    strlcpy(globalState.tips[idx].message, buf, SYSTEM_TIP_MAX_LEN);

    globalState.tip_index = (idx + 1) % SYSTEM_TIPS_MAX;
    if (globalState.tip_count < SYSTEM_TIPS_MAX) {
        globalState.tip_count++;
    }
}

void SystemManagement::addTipRaw(const char* fmt, ...) {
    if (fmt == nullptr || fmt[0] == '\0') return;

    char buf[SYSTEM_TIP_MAX_LEN];
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    int off = snprintf(buf, sizeof(buf), "[%d/%d %02d:%02d]",
                       t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);

    uint8_t idx = globalState.tip_index;
    globalState.tips[idx].timestamp = (uint32_t)now;
    strlcpy(globalState.tips[idx].message, buf, SYSTEM_TIP_MAX_LEN);

    globalState.tip_index = (idx + 1) % SYSTEM_TIPS_MAX;
    if (globalState.tip_count < SYSTEM_TIPS_MAX) {
        globalState.tip_count++;
    }
}

void SystemManagement::clearTips() {
    memset(globalState.tips, 0, sizeof(globalState.tips));
    globalState.tip_count = 0;
    globalState.tip_index = 0;
}

// =============================================================================
// 自消耗分析 - FreeRTOS 任务和核心逻辑
// =============================================================================

// 扫描参数常量
static constexpr uint16_t MIN_BLOCK_MINS = 480;      // 最短连续静置段 8 小时
static constexpr uint32_t MIN_TOTAL_DURATION = 28800; // 累计静置 ≥ 8 小时

void SystemManagement::scAnalysisTask(void* param) {
    SystemManagement* self = static_cast<SystemManagement*>(param);

    // 首次启动延迟 5 分钟（等待系统稳定、NTP 同步）
    vTaskDelay(pdMS_TO_TICKS(300000));

    while (true) {
        uint32_t now = getTimestamp();
        uint32_t cutoff_ts = (now > 1000000000) ? now - (7 * 86400) : 0;

        RawScanResult scan_result = {};
        if (self->scanRawSamples(cutoff_ts, scan_result)) {
            self->runSelfConsumptionAnalysis(scan_result);
        }

        self->analyzeBurstIR();

        // 输出历史最小剩余栈（high water mark），用于监控是否接近溢出
        UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(nullptr);
        DBG.printf_P(PSTR("SC: done, stack min free=%u bytes\n"),
                     (unsigned)stack_hwm * sizeof(StackType_t));

        // 每 24 小时执行一次（分段延时，避免 86400000 溢出 TickType_t）
        for (int i = 0; i < 24; i++) {
            vTaskDelay(pdMS_TO_TICKS(3600000));
        }
    }
}

void SystemManagement::runSelfConsumptionAnalysis(const RawScanResult& scan_result) {
    if (!bms) return;

    DBG.printf_P(PSTR("SC: free heap=%u bytes\n"), (unsigned)ESP.getFreeHeap());

    uint32_t now = getTimestamp();
    if (now < 1000000000) return;

    SCAnalysisResult result = {};
    if (!findQuiescentSegments(now, scan_result, result)) {
        // 分析失败，更新检查时间和总段数
        bms->updateSCLastCheck(now, result.total_segments);
        DBG.printf_P(PSTR("SC: 分析未通过，共 %d 段, free heap=%u\n"), result.total_segments, (unsigned)ESP.getFreeHeap());
        return;
    }

    if (result.valid) {
        bms->updateSelfConsumption(result.self_consumption_mA, result.segment_count,
                                   result.total_segments, now, now);
    }

    DBG.printf_P(PSTR("SC: done, free heap=%u\n"), (unsigned)ESP.getFreeHeap());
}

// =============================================================================
// 合并扫描函数：扫描静置段
// =============================================================================

bool SystemManagement::scanRawSamples(uint32_t cutoff_ts, RawScanResult& result) {
    result.seg_count = 0;

    // 堆分配缓冲区
    static const int MAX_RAW_FILES = 32;
    const int CHUNK = 32;
    char (*filenames)[32] = (char(*)[32])calloc(MAX_RAW_FILES, 32);
    RawSample* buf = (RawSample*)malloc(CHUNK * sizeof(RawSample));
    if (!filenames || !buf) {
        free(filenames);
        free(buf);
        DBG.println(F("SCAN: malloc failed"));
        return false;
    }

    // === 阶段0：收集文件名并排序（快速操作，持锁完成） ===
    int file_count = 0;
    if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);
    {
        File root = SPIFFS.open("/raw");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file && file_count < MAX_RAW_FILES) {
                const char* name = file.name();
                char fullpath[32];
                if (name[0] == '/') {
                    strlcpy(fullpath, name, 32);
                } else {
                    snprintf(fullpath, 32, "/raw/%s", name);
                }
                if (strstr(fullpath, "burst") == nullptr) {
                    strlcpy(filenames[file_count], fullpath, 32);
                    file_count++;
                }
                file = root.openNextFile();
            }
            root.close();
        }
    }
    if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);

    // 按文件名排序（YYYYMMDD.bin 字典序=时间序）
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = i + 1; j < file_count; j++) {
            if (strcmp(filenames[i], filenames[j]) > 0) {
                char tmp[32];
                strlcpy(tmp, filenames[i], 32);
                strlcpy(filenames[i], filenames[j], 32);
                strlcpy(filenames[j], tmp, 32);
            }
        }
    }

    // 只取最近 7 天的文件（分析窗口 = 7 天，避免全量扫描）
    static const int MAX_SCAN_DAYS = 7;
    if (file_count > MAX_SCAN_DAYS) {
        int skip = file_count - MAX_SCAN_DAYS;
        memmove(filenames, filenames + skip, MAX_SCAN_DAYS * 32);
        file_count = MAX_SCAN_DAYS;
    }

    if (file_count == 0) {
        free(filenames);
        free(buf);
        return false;
    }

    // 跨文件保持
    uint32_t prev_ts = 0;

    // 当前正在累积的连续静置段
    bool in_block = false;
    uint32_t blk_start_ts = 0, blk_end_ts = 0;
    int16_t blk_temp_min = 127, blk_temp_max = -127;
    uint16_t blk_mins = 0;

    // 最长段记录
    uint32_t longest_start_ts = 0, longest_end_ts = 0;
    uint16_t longest_mins = 0;

    auto finalizeBlock = [&]() {
        if (!in_block) return;
        if (blk_mins >= MIN_BLOCK_MINS && blk_mins > longest_mins) {
            longest_mins = blk_mins;
            longest_start_ts = blk_start_ts;
            longest_end_ts = blk_end_ts;
        }
        in_block = false;
    };

    // === 阶段1：扫描静置段（逐文件持锁，避免长时间阻塞主循环） ===
    for (int fi = 0; fi < file_count; fi++) {
        if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);

        File f = SPIFFS.open(filenames[fi], FILE_READ);
        if (!f) {
            if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
            continue;
        }

        if (f.size() == 0) {
            f.close();
            if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
            continue;
        }

        while (f.available() >= (int)sizeof(RawSample)) {
            int to_read = min(CHUNK, (int)(f.available() / sizeof(RawSample)));
            if (to_read == 0) break;
            f.read((uint8_t*)buf, to_read * sizeof(RawSample));

            for (int i = 0; i < to_read; i++) {
                RawSample& s = buf[i];

                if (s.timestamp < cutoff_ts) {
                    prev_ts = s.timestamp;
                    continue;
                }

                if (prev_ts > 0 && s.timestamp > prev_ts + 600) {
                    finalizeBlock();
                }

                bool is_quiet = (abs(s.current_mA) < 5);

                if (is_quiet) {
                    if (!in_block) {
                        in_block = true;
                        blk_start_ts = s.timestamp;
                        blk_temp_min = 127;
                        blk_temp_max = -127;
                        blk_mins = 0;
                    }
                    blk_end_ts = s.timestamp;
                    blk_mins++;
                    if (s.temperature < blk_temp_min) blk_temp_min = s.temperature;
                    if (s.temperature > blk_temp_max) blk_temp_max = s.temperature;
                } else {
                    finalizeBlock();
                }

                prev_ts = s.timestamp;
            }
        }

        f.close();
        if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
    }

    finalizeBlock();

    if (longest_mins == 0) {
        free(filenames);
        free(buf);
        return true;
    }

    // === 阶段2：定点回读最长段（逐文件持锁） ===
    static constexpr int TRIM = 10;
    static constexpr int AVG_N = 10;
    float v_first_sum = 0, v_last_sum = 0;
    uint16_t v_first_cnt = 0, v_last_cnt = 0;
    uint32_t trimmed_ts_start = 0, trimmed_ts_end = 0;
    int16_t trimmed_temp_min = 127, trimmed_temp_max = -127;
    uint16_t sample_in_block = 0;
    uint16_t block_total = longest_mins;

    uint16_t skip_start = TRIM;
    uint16_t avg_start_end = TRIM + AVG_N;
    uint16_t avg_end_start = (block_total > TRIM + AVG_N) ? block_total - TRIM - AVG_N : avg_start_end;
    uint16_t skip_end = (block_total > TRIM) ? block_total - TRIM : block_total;

    uint8_t cell_count = (bms && bms->config_.cell_count > 0) ? bms->config_.cell_count : 5;

    for (int fi = 0; fi < file_count; fi++) {
        if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);

        File f = SPIFFS.open(filenames[fi], FILE_READ);
        if (!f) {
            if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
            continue;
        }

        while (f.available() >= (int)sizeof(RawSample)) {
            int to_read = min(CHUNK, (int)(f.available() / sizeof(RawSample)));
            if (to_read == 0) break;
            f.read((uint8_t*)buf, to_read * sizeof(RawSample));

            for (int i = 0; i < to_read; i++) {
                RawSample& s = buf[i];
                if (s.timestamp < longest_start_ts || s.timestamp > longest_end_ts) continue;
                if (abs(s.current_mA) >= 5) continue;

                if (sample_in_block == 0) {
                    trimmed_ts_start = s.timestamp;
                }
                trimmed_ts_end = s.timestamp;
                if (s.temperature < trimmed_temp_min) trimmed_temp_min = s.temperature;
                if (s.temperature > trimmed_temp_max) trimmed_temp_max = s.temperature;

                float sample_cell_avg = 0;
                for (int c = 0; c < cell_count && c < 5; c++) sample_cell_avg += s.cell_mv[c];
                sample_cell_avg /= (float)cell_count;

                if (sample_in_block >= skip_start && sample_in_block < avg_start_end) {
                    v_first_sum += sample_cell_avg;
                    v_first_cnt++;
                }
                if (sample_in_block >= avg_end_start && sample_in_block < skip_end) {
                    v_last_sum += sample_cell_avg;
                    v_last_cnt++;
                }

                sample_in_block++;
            }
        }

        f.close();
        if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
    }

    // === 阶段3：计算结果（纯计算，不需要锁） ===
    if (v_first_cnt > 0 && v_last_cnt > 0 && bms) {
        float v_first_avg = v_first_sum / v_first_cnt;
        float v_last_avg = v_last_sum / v_last_cnt;

        result.seg_count = 1;
        QuiescentSegment& seg = result.segments[0];
        seg.start_ts = trimmed_ts_start;
        seg.end_ts = trimmed_ts_end;
        seg.soc_start = bms->calculateSOC_FromVoltage(v_first_avg);
        seg.soc_end = bms->calculateSOC_FromVoltage(v_last_avg);
        seg.temp_min = trimmed_temp_min;
        seg.temp_max = trimmed_temp_max;
        seg.block_mins = sample_in_block;
    }

    free(filenames);
    free(buf);
    return true;
}

bool SystemManagement::findQuiescentSegments(uint32_t now_ts, const RawScanResult& scan_result, SCAnalysisResult& result) {
    result.total_segments = scan_result.seg_count;

    // scanRawSamples只返回0或1个段
    if (scan_result.seg_count == 0) return false;

    const QuiescentSegment& seg = scan_result.segments[0];
    result.total_segments = 1;

    // SOC 范围筛选（10%-90%，避开OCV曲线极平坦区域）
    if (seg.soc_start < 10 || seg.soc_start > 90) return false;
    if (seg.soc_end < 10 || seg.soc_end > 90) return false;

    // 累计静置时长 ≥ 8 小时
    uint32_t total_quiet_secs = (uint32_t)seg.block_mins * 60;
    if (total_quiet_secs < MIN_TOTAL_DURATION) return false;

    // 计算自消耗（使用 SOH 修正后的实际容量）
    float span_hours = (float)(seg.end_ts - seg.start_ts) / 3600.0f;
    if (span_hours <= 0.01f) return false;

    float q_nominal = (float)bms->config_.nominal_capacity_mAh;
    float soh = globalState.bms.soh;
    if (soh <= 0 || soh > 100) soh = 100.0f;
    float q_actual = q_nominal * soh / 100.0f;

    // 直接用SOC差值计算
    float delta_soc = fabsf(seg.soc_start - seg.soc_end);  // %
    float soc_rate = delta_soc / span_hours;                // %/h
    float sc_current = q_actual * soc_rate / 100.0f;       // mA

    // 温度归一化到 25°C
    float t_avg = (seg.temp_min + seg.temp_max) / 2.0f;
    float sc_25c = sc_current / (1.0f + 0.015f * (t_avg - 25.0f));

    // 合理性检查：自消耗上限 20mA（约480mAh/天）
    if (sc_25c > 20.0f) return false;

    // 调试信息
    DBG.printf_P(PSTR("SC: %.4f%%->%.4f%% dSOC:%.4f%% t:%.1fh sc:%.3fmA\n"),
                 seg.soc_start, seg.soc_end, delta_soc, span_hours, sc_25c);

    result.self_consumption_mA = sc_25c;
    result.segment_count = 1;
    result.valid = true;

    return true;
}

// =============================================================================
// 内阻估算分析（从 burst.bin 读取电流突变快照）
// =============================================================================

void SystemManagement::analyzeBurstIR() {
    if (!bms) return;

    if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);

    File file = SPIFFS.open("/raw/burst.bin", FILE_READ);
    if (!file) {
        if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
        return;
    }

    uint32_t file_size = file.size();
    uint32_t entry_count = file_size / sizeof(BurstSample);
    if (entry_count == 0) {
        file.close();
        if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
        return;
    }

    static constexpr uint32_t MAX_BURST_SAMPLES = 50;
    static constexpr uint8_t  IR_RECENT_COUNT = 10;
    const int CHUNK = 20;

    BurstSample* recent_buf = (BurstSample*)malloc(MAX_BURST_SAMPLES * sizeof(BurstSample));
    BurstSample* buf = (BurstSample*)malloc(CHUNK * sizeof(BurstSample));
    if (!recent_buf || !buf) {
        free(recent_buf);
        free(buf);
        file.close();
        if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
        return;
    }

    uint32_t recent_count = 0;
    uint32_t discard_count = (entry_count > MAX_BURST_SAMPLES) ? entry_count - MAX_BURST_SAMPLES : 0;
    uint32_t sample_idx = 0;

    while (file.available() >= (int)sizeof(BurstSample)) {
        int to_read = min(CHUNK, (int)(file.available() / sizeof(BurstSample)));
        if (to_read == 0) break;
        file.read((uint8_t*)buf, to_read * sizeof(BurstSample));

        for (int i = 0; i < to_read; i++) {
            if (sample_idx >= discard_count && recent_count < MAX_BURST_SAMPLES) {
                recent_buf[recent_count++] = buf[i];
            }
            sample_idx++;
        }
    }

    file.close();

    if (discard_count > 0) {
        File wf = SPIFFS.open("/raw/burst.bin", FILE_WRITE);
        if (wf) {
            if (recent_count > 0) {
                wf.write((uint8_t*)recent_buf, recent_count * sizeof(BurstSample));
            }
            wf.close();
        }
        DBG.printf_P(PSTR("IR: burst.bin trimmed %d -> %d\n"), entry_count, recent_count);
    }

    free(buf);  // 读取完成，释放读缓冲区

    if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);

    float ir_sum[5] = {};
    uint8_t ir_valid_count[5] = {};
    uint8_t ir_start = (recent_count > IR_RECENT_COUNT) ? recent_count - IR_RECENT_COUNT : 0;

    for (uint32_t i = ir_start; i < recent_count; i++) {
        BurstSample& bs = recent_buf[i];
        int16_t delta_current = bs.current_after - bs.current_before;
        if (delta_current == 0) continue;

        for (uint8_t ch = 0; ch < 5; ch++) {
            if (bs.cell_after[ch] == 0) continue;

            int16_t delta_mv = (int16_t)bs.cell_after[ch] - (int16_t)bs.cell_before[ch];
            float r_mΩ = fabsf((float)delta_mv / (float)delta_current * 1000.0f);

            if (r_mΩ > 0.5f && r_mΩ < 500.0f) {
                ir_sum[ch] += r_mΩ;
                ir_valid_count[ch]++;
            }
        }
    }

    float ir_result[5] = {};
    bool all_valid = true;
    for (uint8_t i = 0; i < bms->config_.cell_count; i++) {
        if (ir_valid_count[i] >= 1) {
            ir_result[i] = ir_sum[i] / ir_valid_count[i];
        } else {
            all_valid = false;
        }
    }

    if (all_valid) {
        bms->updateInternalResistance(ir_result, entry_count);
        DBG.printf_P(PSTR("IR: %.1f/%.1f/%.1f/%.1f/%.1fmΩ %d样本\n"),
                     ir_result[0], ir_result[1], ir_result[2], ir_result[3], ir_result[4], entry_count);
    } else {
        DBG.printf_P(PSTR("IR: burst样本不足 %d 条\n"), entry_count);
    }

    free(recent_buf);
}
