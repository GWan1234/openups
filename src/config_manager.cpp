#include "config_manager.h"
#include "Arduino.h"
#include "pins_config.h"
#include "event_bus.h"
#include "event_types.h"
#include "debug.h"
#include "i18n.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

// SPIFFS 互斥锁（定义于 system_management.cpp，保护 /log /raw 等并发文件访问）
extern SemaphoreHandle_t g_spiffs_mutex;

// Webhook 配置在 SPIFFS 中的存储路径（NVS 20KB 无法容纳 3849 字节的结构体）
#define WEBHOOK_CONFIG_PATH "/webhook_cfg.json"

// 静态实例指针，供事件回调使用
static ConfigManager* s_configManagerInstance = nullptr;

// 仅用于 NVS 迁移的 v1 冻结布局（对应加 config_version/chemistry 之前的 BMS_Config_t）
// 注意：sizeof(BMS_Config_v1_t) == sizeof(BMS_Config_t)（对齐填充抵消了新增字段），
// 因此 v2 使用新 key "bms_cfg2" 区分，绝不能只靠长度判断版本。
typedef struct {
    uint8_t cell_count;
    uint32_t nominal_capacity_mAh;
    uint16_t cell_ov_threshold;
    uint16_t cell_uv_threshold;
    uint16_t cell_ov_recover;
    uint16_t cell_uv_recover;
    uint16_t max_charge_current;
    uint16_t max_discharge_current;
    uint16_t short_circuit_threshold;
    float temp_overheat_threshold;
    bool balancing_enabled;
    float balancing_voltage_diff;
} BMS_Config_v1_t;

ConfigManager::ConfigManager() : m_isConfigModeRequired(false) {
    web_username_[0] = '\0';
    web_password_[0] = '\0';
    loadDefaults();
}

void ConfigManager::begin() {
    s_configManagerInstance = this;
    EventBus::getInstance().subscribe(EVT_CONFIG_SYSTEM_CHANGE_REQUEST, onConfigChangeRequest);
}

bool ConfigManager::loadConfiguration(bool forceReset) {
    DBG.println(F("Loading configuration from flash..."));

    // 加载 Web 访问凭证（独立 NVS key，与主配置结构体解耦，
    // 旧固件升级后主配置有效但凭证为空 → Web 层强制先设置账户）
    if (!forceReset) {
        preferences.begin("ups_config", true);
        size_t ulen = preferences.getBytes("web_user", web_username_, sizeof(web_username_) - 1);
        size_t plen = preferences.getBytes("web_pass", web_password_, sizeof(web_password_) - 1);
        preferences.end();
        web_username_[ulen] = '\0';
        web_password_[plen] = '\0';
    }

    // 1. 优先判断：如果强制重置，直接恢复默认值并进入配置模式
    // （硬件按键强制重置同时清除访问凭证 —— 忘记密码时的唯一恢复路径，
    //  重置后进入配置模式，向导会强制设置新账户）
    if (forceReset) {
        DBG.println(F("Force reset requested - resetting to defaults"));
        clearWebCredentials();
        resetToDefaults();
        m_isConfigModeRequired = true;

        // 保存默认配置到 Flash
        writeToFlash();
        return true;
    }
    
    // 2. 尝试从 NVS 读取配置
    preferences.begin("ups_config", true); // ReadOnly mode
    
    // Load all configurations together
    Configuration loadedSystemConfig;
    BMS_Config_t loadedBMSConfig;
    Power_Config_t loadedPowerConfig;
    
    size_t systemBytes = preferences.getBytes("sys_config", &loadedSystemConfig, sizeof(Configuration));
    size_t bmsBytes = preferences.getBytes("bms_cfg2", &loadedBMSConfig, sizeof(BMS_Config_t));
    bool bmsMigrated = false;
    if (bmsBytes != sizeof(BMS_Config_t) ||
        loadedBMSConfig.config_version != BMS_CONFIG_VERSION) {
        // 尝试旧结构迁移（升级用户绝不能被打回配置模式）
        BMS_Config_v1_t v1;
        size_t v1Bytes = preferences.getBytes("bms_config", &v1, sizeof(BMS_Config_v1_t));
        if (v1Bytes == sizeof(BMS_Config_v1_t) && v1.cell_count >= 3 && v1.cell_count <= 5) {
            memset(&loadedBMSConfig, 0, sizeof(BMS_Config_t));
            loadedBMSConfig.config_version        = BMS_CONFIG_VERSION;
            loadedBMSConfig.chemistry             = CHEM_NCM;   // 旧配置一律 NCM，绝不猜化学
            loadedBMSConfig.cell_count            = v1.cell_count;
            loadedBMSConfig.nominal_capacity_mAh  = v1.nominal_capacity_mAh;
            loadedBMSConfig.cell_ov_threshold     = v1.cell_ov_threshold;
            loadedBMSConfig.cell_uv_threshold     = v1.cell_uv_threshold;
            loadedBMSConfig.cell_ov_recover       = v1.cell_ov_recover;
            loadedBMSConfig.cell_uv_recover       = v1.cell_uv_recover;
            loadedBMSConfig.max_charge_current    = v1.max_charge_current;
            loadedBMSConfig.max_discharge_current = v1.max_discharge_current;
            loadedBMSConfig.short_circuit_threshold = v1.short_circuit_threshold;
            loadedBMSConfig.temp_overheat_threshold = v1.temp_overheat_threshold;
            loadedBMSConfig.balancing_enabled     = v1.balancing_enabled;
            loadedBMSConfig.balancing_voltage_diff = v1.balancing_voltage_diff;
            bmsBytes = sizeof(BMS_Config_t);   // 让后续有效性判断通过
            bmsMigrated = true;
            DBG.println(F("BMS config migrated v1 -> v2 (chemistry=NCM)"));
        }
    }
    size_t powerBytes = preferences.getBytes("power_config", &loadedPowerConfig, sizeof(Power_Config_t));

    preferences.end();

    // 迁移成功立即回写新 key，下次启动直接走 v2 路径
    if (bmsMigrated) {
        preferences.begin("ups_config", false);
        preferences.putBytes("bms_cfg2", &loadedBMSConfig, sizeof(BMS_Config_t));
        preferences.end();
    }

    // 3. 检查读取是否成功
    bool systemValid = (systemBytes == sizeof(Configuration)) && validateSystemConfig(loadedSystemConfig);
    bool bmsValid = (bmsBytes == sizeof(BMS_Config_t)) && validateBMSConfig(loadedBMSConfig);
    bool powerValid = (powerBytes == sizeof(Power_Config_t)) && validatePowerConfig(loadedPowerConfig);

    // 交叉校验（仅打日志，不阻断加载）
    if (bmsValid && powerValid) {
        validateCrossConfig(loadedBMSConfig, loadedPowerConfig, false);
    }

    // 4. 核心业务完整性判断
    bool allValid = systemValid && bmsValid && powerValid;
    
    if (!allValid) {
        DBG.println(F("Configuration read failed or invalid - entering config mode"));
        
        // 打印详细错误信息
        if (!systemValid) {
            if (systemBytes > 0) {
                DBG.println(F("  - System configuration invalid or incomplete"));
            } else {
                DBG.println(F("  - System configuration not found"));
            }
        }
        if (!bmsValid) {
            if (bmsBytes > 0) {
                DBG.println(F("  - BMS configuration invalid or incomplete"));
            } else {
                DBG.println(F("  - BMS configuration not found"));
            }
        }
        if (!powerValid) {
            if (powerBytes > 0) {
                DBG.println(F("  - Power configuration invalid or incomplete"));
            } else {
                DBG.println(F("  - Power configuration not found"));
            }
        }
        
        // 恢复默认值并进入配置模式（会自动生成新的identifier）
        resetToDefaults();
        m_isConfigModeRequired = true;
        
        // 保存默认配置到 Flash
        writeToFlash();
        
        // 加载语言设置（独立于主配置）
        I18n::loadLanguage();
        return true;
    }
    
    // 5. 所有检查通过，使用加载的配置
    DBG.println(F("Configuration loaded and validated successfully"));
    systemConfig = loadedSystemConfig;
    bmsConfig = loadedBMSConfig;
    powerConfig = loadedPowerConfig;
    
    // 设置为正常运行模式
    m_isConfigModeRequired = false;
    
    // 加载语言设置（独立于主配置）
    I18n::loadLanguage();
    return false;
}

bool ConfigManager::saveConfiguration() {
    DBG.println(F("Saving all configuration to flash immediately..."));
    return writeToFlash();
}

void ConfigManager::resetWiFiConfig() {
    DBG.println(F("Resetting WiFi configuration to defaults..."));
    
    // 仅重置 WiFi 相关配置，保留其他系统配置
    memset(systemConfig.wifi_ssid, 0, sizeof(systemConfig.wifi_ssid));
    memset(systemConfig.wifi_pass, 0, sizeof(systemConfig.wifi_pass));
    
    // 发布 WiFi 配置变更事件
    EventBus::getInstance().publish(EVT_CONFIG_WIFI_CHANGED, &systemConfig);
    
    // 保存到 Flash
    writeToFlash();
    
}

void ConfigManager::resetConfiguration() {
    DBG.println(F("Resetting configuration to defaults..."));

    // Clear flash storage
    // preferences.clear() 同时清除 web_user/web_pass 访问凭证：
    // 出厂重置（物理按键）是忘记密码时唯一的恢复路径，重置后进入
    // 配置模式，向导会强制设置新账户
    preferences.begin("ups_config", false);
    preferences.clear();
    preferences.end();
    web_username_[0] = '\0';
    web_password_[0] = '\0';

    // Load default values
    resetToDefaults();
    
    // 设置配置模式标志
    m_isConfigModeRequired = true;
}

bool ConfigManager::updateSystemConfig(const Configuration& config, bool immediate) {
    if (!validateSystemConfig(config)) {
        DBG.println(F("Invalid system configuration"));
        return false;
    }
    
    // 检查系统配置是否发生变化（用于事件触发）
    bool systemConfigChanged = (systemConfig.led_brightness != config.led_brightness) ||
                        (systemConfig.buzzer_volume != config.buzzer_volume) ||
                        (systemConfig.buzzer_enabled != config.buzzer_enabled) ||
                        (systemConfig.xiaomi_sensor_enabled != config.xiaomi_sensor_enabled);
    
    // 检查 WiFi 配置是否发生变化
    bool wifiConfigChanged = (strcmp(systemConfig.wifi_ssid, config.wifi_ssid) != 0) ||
                            (strcmp(systemConfig.wifi_pass, config.wifi_pass) != 0);
    
    // 检查 HID 配置是否发生变化
    bool hidConfigChanged = (systemConfig.hid_enabled != config.hid_enabled) ||
                           (systemConfig.hid_report_mode != config.hid_report_mode);
    
    // Update configuration in memory
    systemConfig = config;
    DBG.println(F("System configuration updated in memory"));
    
    // 如果系统配置发生变化，发布系统配置事件
    if (systemConfigChanged) {
        DBG.println(F("[ConfigMgr] System config changed - publishing event"));
        EventBus::getInstance().publish(EVT_CONFIG_SYSTEM_CHANGED, &systemConfig);
    }
    
    // 如果 WiFi 配置发生变化，发布 WiFi 配置事件
    if (wifiConfigChanged) {
        DBG.println(F("[ConfigMgr] WiFi config changed - publishing event"));
        EventBus::getInstance().publish(EVT_CONFIG_WIFI_CHANGED, &systemConfig);
    }
    
    // 如果 HID 配置发生变化，发布 UPS 配置事件
    if (hidConfigChanged) {
        DBG.println(F("[ConfigMgr] Publishing EVT_CONFIG_UPS_CHANGED event"));
        EventBus::getInstance().publish(EVT_CONFIG_UPS_CHANGED, &systemConfig);
    }
    
    // Optionally save to flash immediately
    if (immediate) {
        DBG.println(F("Saving system configuration to flash immediately..."));
        return writeToFlash();
    }
    
    return true;
}

bool ConfigManager::updateBMSConfig(const BMS_Config_t& config, bool immediate) {
    if (!validateBMSConfig(config)) {
        DBG.println(F("Invalid BMS configuration"));
        return false;
    }
    
    // Update configuration in memory
    bmsConfig = config;
 
    // Optionally save to flash immediately
    if (immediate) {
        DBG.println(F("Saving BMS configuration to flash immediately..."));
        return writeToFlash();
    }
    
    return true;
}

bool ConfigManager::updatePowerConfig(const Power_Config_t& config, bool immediate) {
    if (!validatePowerConfig(config)) {
        DBG.println(F("Invalid power configuration"));
        return false;
    }
    
    // Update configuration in memory
    powerConfig = config;
    
    // Optionally save to flash immediately
    if (immediate) {
        DBG.println(F("Saving power configuration to flash immediately..."));
        return writeToFlash();
    }
    
    return true;
}

// Private helper methods

void ConfigManager::loadDefaults() {
    loadSystemDefaults();
    loadBMSDefaults();
    loadPowerDefaults();
}

void ConfigManager::loadSystemDefaults() {
    // System configuration defaults
    memset(&systemConfig, 0, sizeof(Configuration));
    
    // 生成随机设备标识符 (4个字符: 0-9a-z)
    generateDeviceIdentifier(systemConfig.identifier, sizeof(systemConfig.identifier));
    DBG.printf_P(PSTR("Generated device identifier: %s\n"), systemConfig.identifier);
    
    systemConfig.buzzer_enabled = true;
    systemConfig.buzzer_volume = 100;  // 100% 音量
    systemConfig.led_brightness = 30; // 30% 亮度
    systemConfig.hid_enabled = true;  // 默认启用 HID 服务
    systemConfig.hid_report_mode = 2; // 默认百分比模式 (0: mAh, 1: mWh, 2: %)

    // 静态 IP 配置默认值 - 默认使用 DHCP
    systemConfig.use_static_ip = false;
    strncpy(systemConfig.static_ip, "192.168.1.100", sizeof(systemConfig.static_ip) - 1);
    strncpy(systemConfig.static_gateway, "192.168.1.1", sizeof(systemConfig.static_gateway) - 1);
    strncpy(systemConfig.static_subnet, "255.255.255.0", sizeof(systemConfig.static_subnet) - 1);
    strncpy(systemConfig.static_dns, "8.8.8.8", sizeof(systemConfig.static_dns) - 1);

    // NTP 服务器默认值
    strncpy(systemConfig.ntp_server, "ntp.aliyun.com", sizeof(systemConfig.ntp_server) - 1);
}

void ConfigManager::loadBMSDefaults() {
    // BMS configuration defaults - 使用安全的保守值
    bmsConfig = BMS::getDefaultConfig(3, CHEM_NCM); // 3 串三元锂默认配置
}

void ConfigManager::loadPowerDefaults() {
    // Power configuration defaults - 使用安全的保守值
    powerConfig = PowerManagement::getDefaultConfig();
    // 充电电压按 BMS 化学与串数派生（loadBMSDefaults 先于本函数执行）
    powerConfig.charge_voltage_limit = (uint16_t)(bmsConfig.cell_count *
        getChemistryLimits((BatteryChemistry_t)bmsConfig.chemistry).recommended_charge_cell_mV);
}

bool ConfigManager::validateSystemConfig(const Configuration& config) {
    // Basic validation
    
    // 验证设备标识符：必须为4个字符，且为0-9a-z
    if (strlen(config.identifier) != 4) {
        DBG.printf_P(PSTR("  Invalid device identifier length: %d (must be 4)\n"), strlen(config.identifier));
        return false;
    }
    
    for (int i = 0; i < 4; i++) {
        char c = config.identifier[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) {
            DBG.printf_P(PSTR("  Invalid device identifier character: '%c' at position %d\n"), c, i);
            return false;
        }
    }
    
    // Validate WiFi SSID (can be empty for AP mode)
    // Validate LED brightness range
    if (config.led_brightness > 100) {
        DBG.println(F("  LED brightness out of range (0-100)"));
        return false;
    }
    
    // Validate buzzer volume range
    if (config.buzzer_volume > 100) {
        DBG.println(F("  Buzzer volume out of range (0-100)"));
        return false;
    }
    
    // Validate hid_report_mode range
    if (config.hid_report_mode > 2) {
        DBG.println(F("  HID report mode out of range (0-2)"));
        return false;
    }
    
    // 验证静态 IP 配置（如果启用）
    if (config.use_static_ip) {
        if (!isValidIPAddress(config.static_ip)) {
            DBG.println(F("  Invalid static IP address format"));
            return false;
        }
        if (!isValidIPAddress(config.static_gateway)) {
            DBG.println(F("  Invalid gateway IP address format"));
            return false;
        }
        if (!isValidIPAddress(config.static_subnet)) {
            DBG.println(F("  Invalid subnet mask format"));
            return false;
        }
        if (!isValidIPAddress(config.static_dns)) {
            DBG.println(F("  Invalid DNS server IP address format"));
            return false;
        }
    }
    
    return true;
}

// IP 地址格式验证辅助函数
bool ConfigManager::isValidIPAddress(const char* ip) {
    if (ip == nullptr || strlen(ip) == 0) {
        return false;
    }
    
    int num, dots = 0;
    const char* ptr = ip;
    
    while (*ptr) {
        // 提取数字
        num = 0;
        while (*ptr && *ptr != '.') {
            if (*ptr < '0' || *ptr > '9') {
                return false; // 非数字字符
            }
            num = num * 10 + (*ptr - '0');
            ptr++;
        }
        
        // 检查数字范围 (0-255)
        if (num < 0 || num > 255) {
            return false;
        }
        
        // 检查点号
        if (*ptr == '.') {
            dots++;
            ptr++;
            if (dots > 3) {
                return false; // 超过 3 个点
            }
        } else if (*ptr != '\0') {
            return false; // 非法字符
        }
    }
    
    // 必须恰好有 3 个点（4 个数字）
    return (dots == 3);
}

// 生成随机设备标识符 (4个字符: 0-9a-z)
void ConfigManager::generateDeviceIdentifier(char* identifier, size_t size) {
    if (size < 5) { // 需要至少5字节空间 (4字符 + 结束符)
        DBG.println(F("Warning: identifier buffer too small"));
        strncpy(identifier, "0000", size - 1);
        identifier[size - 1] = '\0';
        return;
    }
    
    // 字符集: 0-9 (10个) + a-z (26个) = 36个字符
    const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    const size_t charset_size = sizeof(charset) - 1; // 36
    
    // 使用硬件随机数生成器
    randomSeed(esp_random());
    
    for (int i = 0; i < 4; i++) {
        identifier[i] = charset[random(0, charset_size)];
    }
    identifier[4] = '\0'; // 确保字符串正确终止
}

bool ConfigManager::validateBMSConfig(const BMS_Config_t& config) {
    // 关键业务完整性检查 1: 串数必须在合法范围 (3-5)
    if (config.cell_count < 3 || config.cell_count > 5) {
        DBG.printf_P(PSTR("  Invalid cell count: %d (must be 3-5)\n"), config.cell_count);
        return false;
    }

    if (config.config_version != BMS_CONFIG_VERSION) {
        DBG.println(F("  Invalid BMS config version"));
        return false;
    }
    if (config.chemistry > CHEM_LFP) {
        DBG.println(F("  Invalid battery chemistry"));
        return false;
    }

    // 容量必须有效
    if (config.nominal_capacity_mAh == 0) {
        DBG.println(F("  Invalid nominal capacity: 0"));
        return false;
    }

    // 电压阈值必须合理 (单位：mV)
    if (config.cell_ov_threshold <= config.cell_uv_threshold) {
        DBG.println(F("  OV threshold must be greater than UV threshold"));
        return false;
    }

    // 充电电流必须大于 0
    if (config.max_charge_current == 0) {
        DBG.println(F("  Max charge current is 0"));
        return false;
    }

    return true;
}

bool ConfigManager::validatePowerConfig(const Power_Config_t& config) {
    // 关键业务完整性检查 2: 充电电流必须大于 0
    if (config.max_charge_current == 0) {
        DBG.println(F("  Max charge current is 0"));
        return false;
    }
    
    // 放电电流必须大于 0
    if (config.max_discharge_current == 0) {
        DBG.println(F("  Max discharge current is 0"));
        return false;
    }
    
    // 验证时间窗口配置
    if (config.charging_window_count > 5) {
        DBG.printf_P(PSTR("  Invalid charging window count: %d (max 5)\n"), config.charging_window_count);
        return false;
    }
    
    // 验证每个时间窗口的合法性
    for (uint8_t i = 0; i < config.charging_window_count; i++) {
        const ChargingTimeWindow_t& window = config.charging_windows[i];
        
        // 检查掩码是否有效（至少有一天）
        if (window.day_mask == 0) {
            DBG.printf_P(PSTR("  Window %d has invalid day mask (0)\n"), i);
            return false;
        }
        
        // 检查小时范围
        if (window.start_hour > 23 || window.end_hour > 24) {
            DBG.printf_P(PSTR("  Window %d has invalid hour range (%d-%d)\n"),
                           i, window.start_hour, window.end_hour);
            return false;
        }
        
        // 检查开始时间不能晚于结束时间
        if (window.start_hour >= window.end_hour) {
            DBG.printf_P(PSTR("  Window %d start hour (%d) must be before end hour (%d)\n"), 
                           i, window.start_hour, window.end_hour);
            return false;
        }
    }
    
    return true;
}

// 交叉校验：充电电压不得高于 串数×(单体OV阈值-10mV)。
// strict=true 用于 Web 新输入（违反即拒绝）；
// strict=false 用于 NVS 加载（旧配置可能违反——如 12600 vs 3S/4210，只告警不拒绝，
// 否则升级用户会被打回配置模式）
bool ConfigManager::validateCrossConfig(const BMS_Config_t& bms, const Power_Config_t& power, bool strict) {
    uint32_t max_charge_mv = (uint32_t)bms.cell_count * (bms.cell_ov_threshold - 10);
    if (power.charge_voltage_limit > max_charge_mv) {
        DBG.printf_P(PSTR("  Cross-check: charge_voltage %u > cells*(OV-10)=%u %s\n"),
            power.charge_voltage_limit, max_charge_mv, strict ? "(rejected)" : "(warning)");
        return !strict;
    }
    return true;
}

// 内部重置方法 - 将所有参数设置为安全的保守值
void ConfigManager::resetToDefaults() {
    DBG.println(F("Resetting to safe default values..."));
    
    // 使用 loadDefaults() 加载各模块的默认配置
    // 这样可以保持架构一致性，默认配置由各模块自己提供
    loadDefaults();
    
    DBG.println(F("Safe default values loaded"));
}

bool ConfigManager::writeToFlash() {
    DBG.println(F("Writing configuration to flash..."));    
    preferences.begin("ups_config", false); // ReadWrite mode
    
    bool success = true;
    
    // Load current flash configurations for comparison
    Configuration flashSystemConfig;
    BMS_Config_t flashBMSConfig;
    Power_Config_t flashPowerConfig;
    
    size_t systemBytes = preferences.getBytes("sys_config", &flashSystemConfig, sizeof(Configuration));
    size_t bmsBytes = preferences.getBytes("bms_cfg2", &flashBMSConfig, sizeof(BMS_Config_t));
    size_t powerBytes = preferences.getBytes("power_config", &flashPowerConfig, sizeof(Power_Config_t));
    

    bool systemChanged = (systemBytes != sizeof(Configuration)) || 
                        (memcmp(&systemConfig, &flashSystemConfig, sizeof(Configuration)) != 0);
    bool bmsChanged = (bmsBytes != sizeof(BMS_Config_t)) || 
                     (memcmp(&bmsConfig, &flashBMSConfig, sizeof(BMS_Config_t)) != 0);
    bool powerChanged = (powerBytes != sizeof(Power_Config_t)) || 
                       (memcmp(&powerConfig, &flashPowerConfig, sizeof(Power_Config_t)) != 0);
    
    // Save only changed configurations
    if (systemChanged) {
        if (preferences.putBytes("sys_config", &systemConfig, sizeof(Configuration))) {
            DBG.println(F("System configuration saved to flash"));
        } else {
            DBG.println(F("Failed to save system configuration to flash"));
            success = false;
        }
    } else {
        DBG.println(F("System configuration unchanged, skipping flash write"));
    }
    
    if (bmsChanged) {
        if (preferences.putBytes("bms_cfg2", &bmsConfig, sizeof(BMS_Config_t))) {
            // 发布 BMS 配置变更事件
            EventBus::getInstance().publish(EVT_CONFIG_BMS_CHANGED, &bmsConfig);
            DBG.println(F("BMS configuration saved to flash"));
        } else {
            DBG.println(F("Failed to save BMS configuration to flash"));
            success = false;
        }
    } else {
        DBG.println(F("BMS configuration unchanged, skipping flash write"));
    }
    
    if (powerChanged) {
        if (preferences.putBytes("power_config", &powerConfig, sizeof(Power_Config_t))) {
            // 发布电源配置变更事件
            EventBus::getInstance().publish(EVT_CONFIG_POWER_CHANGED, &powerConfig);
            DBG.println(F("Power configuration saved to flash"));
        } else {
            DBG.println(F("Failed to save power configuration to flash"));
            success = false;
        }
    } else {
        DBG.println(F("Power configuration unchanged, skipping flash write"));
    }
    
    preferences.end();
    
    if (success) {
        DBG.println(F("Configuration write to flash completed successfully"));
    } else {
        DBG.println(F("Configuration write to flash completed with errors"));
    }
    
    return success;
}

// =============================================================================
// Web 访问凭证管理（独立 NVS key 持久化）
// =============================================================================

bool ConfigManager::setWebCredentials(const char* username, const char* password) {
    if (!username || !password) return false;

    size_t ulen = strlen(username);
    size_t plen = strlen(password);
    if (ulen < 1 || ulen > 32) {
        DBG.println(F("[ConfigMgr] Invalid web username length (1-32)"));
        return false;
    }
    if (plen < 8 || plen > 64) {
        DBG.println(F("[ConfigMgr] Invalid web password length (8-64)"));
        return false;
    }

    strlcpy(web_username_, username, sizeof(web_username_));
    strlcpy(web_password_, password, sizeof(web_password_));

    preferences.begin("ups_config", false);
    bool ok = preferences.putBytes("web_user", web_username_, ulen) == ulen &&
              preferences.putBytes("web_pass", web_password_, plen) == plen;
    preferences.end();

    DBG.println(ok ? F("[ConfigMgr] Web credentials saved")
                   : F("[ConfigMgr] Failed to save web credentials"));
    return ok;
}

void ConfigManager::clearWebCredentials() {
    web_username_[0] = '\0';
    web_password_[0] = '\0';
    preferences.begin("ups_config", false);
    preferences.remove("web_user");
    preferences.remove("web_pass");
    preferences.end();
    DBG.println(F("[ConfigMgr] Web credentials cleared"));
}

void ConfigManager::onConfigChangeRequest(EventType type, void* param) {
    if (!s_configManagerInstance || !param) return;
    const Configuration* config = static_cast<const Configuration*>(param);
    s_configManagerInstance->updateSystemConfig(*config, true);
}

// =============================================================================
// Webhook 配置管理
// =============================================================================

bool ConfigManager::loadWebhookConfig(WebhookConfig_t& config) {
    if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);

    File f = SPIFFS.open(WEBHOOK_CONFIG_PATH, FILE_READ);
    if (!f) {
        if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
        DBG.println(F("[ConfigMgr] Webhook config file not found"));
        return false;
    }

    String raw = f.readString();
    f.close();
    if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);

    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, raw);
    if (err) {
        DBG.printf("[ConfigMgr] Webhook config JSON parse failed: %s\n", err.c_str());
        return false;
    }

    memset(&config, 0, sizeof(config));
    config.config_version = doc["config_version"] | (uint16_t)WH_CONFIG_VERSION;
    config.global_enabled = doc["global_enabled"] | false;

    JsonArray endpoints = doc["endpoints"].as<JsonArray>();
    uint8_t count = 0;
    for (JsonObject ep : endpoints) {
        if (count >= WH_MAX_ENDPOINTS) break;
        WebhookEndpoint_t& dest = config.endpoints[count];

        dest.enabled = ep["enabled"] | false;
        dest.verify_tls = ep["verify_tls"] | true;
        dest.method = ep["method"] | (uint8_t)WH_METHOD_POST;
        strlcpy(dest.name, ep["name"] | "", sizeof(dest.name));
        strlcpy(dest.url, ep["url"] | "", sizeof(dest.url));
        strlcpy(dest.auth_token, ep["auth_token"] | "", sizeof(dest.auth_token));
        strlcpy(dest.device_key, ep["device_key"] | "", sizeof(dest.device_key));
        strlcpy(dest.auth_header, ep["auth_header"] | "", sizeof(dest.auth_header));
        dest.cooldown_ms = ep["cooldown_ms"] | (uint32_t)WH_DEFAULT_COOLDOWN;
        strlcpy(dest.message_template, ep["message_template"] | "", sizeof(dest.message_template));

        JsonArray triggers = ep["triggers"].as<JsonArray>();
        uint8_t trigCount = 0;
        for (JsonObject t : triggers) {
            if (trigCount >= WH_MAX_TRIGGERS) break;
            WebhookTrigger_t& tDest = dest.triggers[trigCount];

            tDest.enabled = t["enabled"] | false;
            tDest.alert_level = t["alert_level"] | (uint8_t)0;
            tDest.condition.trigger_type = t["trigger_type"] | (uint8_t)0;
            tDest.condition.source = t["source"] | (uint8_t)0;
            tDest.condition.compare_op = t["compare_op"] | (uint8_t)0;
            tDest.condition.threshold = t["threshold"] | 0.0f;
            strlcpy(tDest.dedup_key, t["dedup_key"] | "", sizeof(tDest.dedup_key));
            strlcpy(tDest.title, t["title"] | "", sizeof(tDest.title));
            strlcpy(tDest.description, t["description"] | "", sizeof(tDest.description));
            tDest.fired = false;

            trigCount++;
        }
        dest.trigger_count = trigCount;

        count++;
    }
    config.endpoint_count = count;

    if (!validateWebhookConfig(config)) {
        DBG.println(F("[ConfigMgr] Webhook config validation failed"));
        return false;
    }

    DBG.printf("[ConfigMgr] Webhook config loaded from SPIFFS: %u endpoints\n", config.endpoint_count);
    return true;
}

bool ConfigManager::saveWebhookConfig(const WebhookConfig_t& config) {
    if (!validateWebhookConfig(config)) {
        DBG.println(F("[ConfigMgr] Webhook config validation failed, not saving"));
        return false;
    }

    DynamicJsonDocument doc(16384);
    doc["config_version"] = config.config_version;
    doc["global_enabled"] = config.global_enabled;

    JsonArray endpoints = doc.createNestedArray("endpoints");
    for (uint8_t i = 0; i < config.endpoint_count && i < WH_MAX_ENDPOINTS; i++) {
        const WebhookEndpoint_t& ep = config.endpoints[i];
        JsonObject obj = endpoints.createNestedObject();

        obj["enabled"] = ep.enabled;
        obj["verify_tls"] = ep.verify_tls;
        obj["method"] = ep.method;
        obj["name"] = ep.name;
        obj["url"] = ep.url;
        obj["auth_token"] = ep.auth_token;
        obj["device_key"] = ep.device_key;
        obj["auth_header"] = ep.auth_header;
        obj["cooldown_ms"] = ep.cooldown_ms;
        obj["message_template"] = ep.message_template;

        JsonArray triggers = obj.createNestedArray("triggers");
        for (uint8_t j = 0; j < ep.trigger_count && j < WH_MAX_TRIGGERS; j++) {
            const WebhookTrigger_t& trig = ep.triggers[j];
            JsonObject tObj = triggers.createNestedObject();

            tObj["enabled"] = trig.enabled;
            tObj["alert_level"] = trig.alert_level;
            tObj["trigger_type"] = trig.condition.trigger_type;
            tObj["source"] = trig.condition.source;
            tObj["compare_op"] = trig.condition.compare_op;
            tObj["threshold"] = trig.condition.threshold;
            tObj["dedup_key"] = trig.dedup_key;
            tObj["title"] = trig.title;
            tObj["description"] = trig.description;
        }
    }

    String out;
    serializeJson(doc, out);

    if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);
    File f = SPIFFS.open(WEBHOOK_CONFIG_PATH, FILE_WRITE);
    bool ok = false;
    if (f) {
        ok = (f.print(out) == out.length());
        f.close();
    } else {
        DBG.println(F("[ConfigMgr] Failed to open webhook config file for write"));
    }
    if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);

    DBG.println(ok ? F("[ConfigMgr] Webhook config saved to SPIFFS")
                   : F("[ConfigMgr] Failed to save webhook config to SPIFFS"));
    return ok;
}

void ConfigManager::loadWebhookDefaults(WebhookConfig_t& config) {
    memset(&config, 0, sizeof(WebhookConfig_t));
    config.config_version = WH_CONFIG_VERSION;
    config.global_enabled = false;
    config.endpoint_count = 0;

    // 初始化每个端点的默认值
    for (int i = 0; i < WH_MAX_ENDPOINTS; i++) {
        config.endpoints[i].cooldown_ms = WH_DEFAULT_COOLDOWN;
        config.endpoints[i].verify_tls = true;   // 默认开启证书校验
        config.endpoints[i].method = WH_METHOD_POST; // 默认 POST
        config.endpoints[i].trigger_count = 0;
    }
}

bool ConfigManager::validateWebhookConfig(const WebhookConfig_t& config, char* reason) {
    #define WH_FAIL(id, ...) do { if (reason) snprintf(reason, 64, I18n::get(id), ##__VA_ARGS__); } while(0)

    if (config.config_version != WH_CONFIG_VERSION) { WH_FAIL(STR_WH_V_VERSION); return false; }
    if (config.endpoint_count > WH_MAX_ENDPOINTS) { WH_FAIL(STR_WH_V_ENDPOINT_COUNT); return false; }

    for (uint8_t i = 0; i < config.endpoint_count; i++) {
        const WebhookEndpoint_t& ep = config.endpoints[i];

        // URL 非空检查 (启用的端点)
        if (ep.enabled && ep.url[0] == '\0') { WH_FAIL(STR_WH_V_URL_EMPTY, i + 1); return false; }

        // 请求方式检查
        if (ep.method > WH_METHOD_GET) { WH_FAIL(STR_WH_V_METHOD, i + 1); return false; }

        // 冷却时间下限 10 秒
        if (ep.cooldown_ms < 10000) { WH_FAIL(STR_WH_V_COOLDOWN, i + 1); return false; }

        // 触发器数量检查
        if (ep.trigger_count > WH_MAX_TRIGGERS) { WH_FAIL(STR_WH_V_TRIGGER_COUNT, i + 1); return false; }

        for (uint8_t j = 0; j < ep.trigger_count; j++) {
            const WebhookTrigger_t& trig = ep.triggers[j];

            // 触发器类型检查
            if (trig.condition.trigger_type > WH_TRIGGER_STATE) { WH_FAIL(STR_WH_V_TRIGGER_TYPE, i + 1); return false; }

            // 比较运算符检查
            if (trig.condition.compare_op > WH_CMP_CHANGE) { WH_FAIL(STR_WH_V_CMP_OP, i + 1); return false; }

            // 交叉校验: 值触发只能用 GT/LT，状态触发只能用 EQ/CHANGE
            if (trig.condition.trigger_type == WH_TRIGGER_VALUE) {
                if (trig.condition.compare_op != WH_CMP_GT &&
                    trig.condition.compare_op != WH_CMP_LT) { WH_FAIL(STR_WH_V_VALUE_OP, i + 1); return false; }
            } else {
                if (trig.condition.compare_op != WH_CMP_EQ &&
                    trig.condition.compare_op != WH_CMP_CHANGE) { WH_FAIL(STR_WH_V_STATE_OP, i + 1); return false; }
            }

            // 值源/状态源范围检查
            if (trig.condition.trigger_type == WH_TRIGGER_VALUE &&
                trig.condition.source >= WH_VALUE_SOURCE_MAX) { WH_FAIL(STR_WH_V_SOURCE_VALUE, i + 1); return false; }
            if (trig.condition.trigger_type == WH_TRIGGER_STATE &&
                trig.condition.source >= WH_STATE_SOURCE_MAX) { WH_FAIL(STR_WH_V_SOURCE_STATE, i + 1); return false; }

            // 告警级别检查
            if (trig.alert_level > WH_LEVEL_CRITICAL) { WH_FAIL(STR_WH_V_LEVEL, i + 1); return false; }
        }
    }

    #undef WH_FAIL
    return true;
}