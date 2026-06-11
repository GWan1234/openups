// 在 cpp 文件中包含 AsyncMQTT_ESP32.h，避免头文件污染导致链接错误
#include <AsyncMQTT_ESP32.h>
#include "mqtt_service.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "event_bus.h"
#include "event_types.h"
#include "debug.h"


// =============================================================================
// 静态成员初始化
// =============================================================================
MQTTService* MQTTService::s_instance = nullptr;

// =============================================================================
// MQTTService 构造函数
// =============================================================================
MQTTService::MQTTService()
    : m_config(nullptr), m_state(nullptr)
    , m_mqtt_client(nullptr)
    , m_use_tls(false), m_configured(false), m_connected(false)
    , m_discovery_published(false)
    , m_last_state_publish(0), m_last_heartbeat(0)
    , m_last_reconnect_attempt(0)
{
    s_instance = this;
    memset(m_device_id, 0, sizeof(m_device_id));
    memset(m_topic_base, 0, sizeof(m_topic_base));
    memset(m_mqtt_broker, 0, sizeof(m_mqtt_broker));
    memset(m_mqtt_username, 0, sizeof(m_mqtt_username));
    memset(m_mqtt_password, 0, sizeof(m_mqtt_password));
    m_mqtt_port = 0;
}

// =============================================================================
// MQTTService 析构函数
// =============================================================================
MQTTService::~MQTTService() {
    disconnect();
    if (m_mqtt_client) {
        delete m_mqtt_client;
        m_mqtt_client = nullptr;
    }
}

bool MQTTService::begin(Configuration* config, System_Global_State* state) {
    if (!config || !state) return false;

    m_config = config;
    m_state = state;
    if (strlen(config->identifier) > 0) {
        snprintf(m_device_id, sizeof(m_device_id), "ups-%s", config->identifier);
    } else {
        snprintf(m_device_id, sizeof(m_device_id), "ups-default");
    }
    snprintf(m_topic_base, sizeof(m_topic_base), "%s", m_device_id);

    // 创建 AsyncMqttClient 实例
    m_mqtt_client = new AsyncMqttClient();
    m_mqtt_client->onConnect([](bool sessionPresent) {
        onMqttConnect(sessionPresent);
    });
    m_mqtt_client->onDisconnect([](AsyncMqttClientDisconnectReason reason) {
        onMqttDisconnect(static_cast<int>(reason));
    });
    m_mqtt_client->onMessage([](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        onMqttMessage(topic, payload, static_cast<int>(properties.qos), len, index, total);
    });
    m_mqtt_client->setKeepAlive(MQTT_KEEPALIVE);

    m_configured = true;
    m_discovery_published = false;
    setStaticInstance(this);
    return true;
}

void MQTTService::setBrokerAddress(const char* broker) {
    if (broker) strncpy(m_mqtt_broker, broker, sizeof(m_mqtt_broker) - 1);
}

void MQTTService::setBrokerPort(uint16_t port) {
    m_mqtt_port = port;
    if (port == 8883) { m_use_tls = true; }
}

void MQTTService::setBrokerCredentials(const char* username, const char* password) {
    if (username) strncpy(m_mqtt_username, username, sizeof(m_mqtt_username) - 1);
    if (password) strncpy(m_mqtt_password, password, sizeof(m_mqtt_password) - 1);
}

void MQTTService::setStaticInstance(MQTTService* instance) { s_instance = instance; }

void MQTTService::loop(System_Global_State& state) {
    m_state = &state;
    
    if (!m_configured) {
        return;
    }

    if (!m_connected) {
        unsigned long now = millis();
        if (now - m_last_reconnect_attempt >= 5000) {
            m_last_reconnect_attempt = now;
            if (checkWifiConnected()) {
                connect();
            } 
        }
        return;
    }

    // AsyncMqttClient 是事件驱动的，不需要 loop() 调用

    unsigned long now = millis();
    if (now - m_last_state_publish >= 10000) {
        publishStateData();
        m_last_state_publish = now;
    }
    if (!m_discovery_published) {
        publishDiscoveryConfig();
        m_discovery_published = true;
    }
    if (now - m_last_heartbeat >= 60000) {
        publishAvailability(true);
        m_last_heartbeat = now;
    }
}

bool MQTTService::isConnected() { return m_connected; }

bool MQTTService::connect() {
    if (!checkWifiConnected()) {
        return false;
    }

    if (m_connected) {
        m_mqtt_client->disconnect();
        m_connected = false;
    }

    // 配置 AsyncMqttClient
    m_mqtt_client->setServer(m_mqtt_broker, m_mqtt_port);
    m_mqtt_client->setClientId(m_device_id);
    m_mqtt_client->setKeepAlive(MQTT_KEEPALIVE);

    if (strlen(m_mqtt_username) > 0) {
        m_mqtt_client->setCredentials(m_mqtt_username, m_mqtt_password);
    }

#if ASYNC_TCP_SSL_ENABLED
    if (m_use_tls) {
        m_mqtt_client->setSecure(true);
    }
#endif
    m_mqtt_client->connect();

    // 返回 true 表示连接尝试已发起，实际结果由 onConnect/onDisconnect 回调更新
    return true;
}

void MQTTService::disconnect() {
    if (m_connected) {
        publishAvailability(false);
    }
    if (m_mqtt_client) {
        m_mqtt_client->disconnect();
    }
    m_connected = false;
}

// =============================================================================
// AsyncMqttClient 回调
// =============================================================================
void MQTTService::onMqttConnect(bool sessionPresent) {
    DBG.printf("[MQTT] onConnect: sessionPresent=%s\n", sessionPresent ? "true" : "false");
    if (s_instance) {
        s_instance->m_connected = true;
        s_instance->subscribeCommandTopics();
        s_instance->publishAvailability(true);
    }
}

void MQTTService::onMqttDisconnect(int reason) {
    DBG.printf("[MQTT] onDisconnect: reason=%d\n", reason);
    if (s_instance) {
        s_instance->m_connected = false;
    }
}

void MQTTService::onMqttMessage(char* topic, char* payload, int properties, size_t len, size_t index, size_t total) {
    if (s_instance) {
        s_instance->handleCommand(topic, (const char*)payload, len);
    }
}

// =============================================================================
// 订阅命令
// =============================================================================
void MQTTService::subscribeCommandTopics() {
    if (!m_connected) {
        return;
    }
    char t[128];
    snprintf(t, sizeof(t), "%s/command/led_brightness/set", m_topic_base);
    m_mqtt_client->subscribe(t, 0);
    snprintf(t, sizeof(t), "%s/command/buzzer_volume/set", m_topic_base);
    m_mqtt_client->subscribe(t, 0);
    snprintf(t, sizeof(t), "%s/command/hid_report_mode/set", m_topic_base);
    m_mqtt_client->subscribe(t, 0);
}

void MQTTService::setDeviceIdentifier(const char* identifier) {
    if (identifier && strlen(identifier)) {
        snprintf(m_device_id, sizeof(m_device_id), "ups-%s", identifier);
        snprintf(m_topic_base, sizeof(m_topic_base), "ups-%s", identifier);
        m_discovery_published = false;
    }
}

// =============================================================================
// 发布辅助
// =============================================================================
bool MQTTService::publishPayload(const char* topic, const uint8_t* payload, size_t length, uint8_t qos, bool retain) {
    if (!m_connected || !m_mqtt_client) return false;
    uint16_t packetId = m_mqtt_client->publish(topic, qos, retain, reinterpret_cast<const char*>(payload), length);
    return packetId != 0;
}

// =============================================================================
// Discovery 配置
// =============================================================================
bool MQTTService::publishDiscoveryConfig() {
    if (!m_connected) {
        return false;
    }

    // 电池传感器
    publishSensorDiscovery("bms_soc", "UPS Battery SOC", "battery", "measurement", "%",
                          "{{ value_json.bms.soc }}");
    publishSensorDiscovery("bms_soh", "UPS Battery SOH", "battery", "measurement", "%",
                          "{{ value_json.bms.soh }}");
    publishSensorDiscovery("bms_voltage", "UPS Battery Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.voltage }}");
    publishSensorDiscovery("bms_current", "UPS Battery Current", "current", "measurement", "mA",
                          "{{ value_json.bms.current }}");
    publishSensorDiscovery("bms_temperature", "UPS Battery Temperature", "temperature", "measurement", "°C",
                          "{{ value_json.bms.temperature }}");
    publishSensorDiscovery("bms_capacity_remaining", "UPS Battery Capacity Remaining", "battery_capacity", "measurement", "mAh",
                          "{{ value_json.bms.capacity_remaining }}");
    publishSensorDiscovery("bms_cycle_count", "UPS Battery Cycle Count", nullptr, "measurement", nullptr,
                          "{{ value_json.bms.cycle_count }}");
    publishSensorDiscovery("bms_self_consumption", "UPS Self Consumption Current", "current", "measurement", "mA",
                          "{{ value_json.bms.self_consumption }}");

    // 单体电压 (Cell 1-5)
    publishSensorDiscovery("bms_cell_1", "UPS Cell 1 Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_1 }}");
    publishSensorDiscovery("bms_cell_2", "UPS Cell 2 Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_2 }}");
    publishSensorDiscovery("bms_cell_3", "UPS Cell 3 Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_3 }}");
    publishSensorDiscovery("bms_cell_4", "UPS Cell 4 Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_4 }}");
    publishSensorDiscovery("bms_cell_5", "UPS Cell 5 Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_5 }}");
    publishSensorDiscovery("bms_cell_min", "UPS Cell Min Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_min }}");
    publishSensorDiscovery("bms_cell_max", "UPS Cell Max Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_max }}");
    publishSensorDiscovery("bms_cell_avg", "UPS Cell Avg Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.bms.cell_avg }}");

    // 电源传感器
    publishSensorDiscovery("power_input_voltage", "UPS Input Voltage", "voltage", "measurement", "mV",
                          "{{ value_json.power.input_voltage }}");
    publishSensorDiscovery("power_input_current", "UPS Input Current", "current", "measurement", "mA",
                          "{{ value_json.power.input_current }}");
    publishSensorDiscovery("power_output_power", "UPS Output Power", "power", "measurement", "W",
                          "{{ value_json.power.output_power }}");
    publishSensorDiscovery("power_battery_voltage", "UPS Battery Voltage(adc)", "voltage", "measurement", "mV",
                          "{{ value_json.power.battery_voltage }}");
    publishSensorDiscovery("power_battery_current", "UPS Battery Current(adc)", "current", "measurement", "mA",
                          "{{ value_json.power.battery_current }}");

    // 二进制传感器
    publishBinarySensorDiscovery("ac_present", "UPS AC Power", "power",
                          "{{ 'true' if value_json.power.ac_present == true else 'false' }}");
    publishBinarySensorDiscovery("charger_enabled", "UPS Charger Enabled", "plug",
                          "{{ 'true' if value_json.power.charger_enabled == true else 'false' }}");
    publishBinarySensorDiscovery("balancing_active", "UPS Balancing Active", "running",
                          "{{ 'true' if value_json.bms.balancing_active == true else 'false' }}");
    publishBinarySensorDiscovery("wifi_connected", "UPS WiFi Connected", "connectivity",
                          "{{ 'true' if value_json.system.wifi_connected == true else 'false' }}");
    publishBinarySensorDiscovery("bms_fault", "UPS BMS Fault", "problem",
                          "{{ 'fault' if value_json.bms.fault_type != 'None' else 'None' }}", true);
    publishBinarySensorDiscovery("power_fault", "UPS Power Fault", "problem",
                          "{{ 'fault' if value_json.power.fault_type != 'None' else 'None' }}", true);
    publishBinarySensorDiscovery("emergency_shutdown", "UPS Emergency Shutdown", "problem",
                          "{{ 'true' if value_json.system.emergency_shutdown == true else 'false' }}");
    publishBinarySensorDiscovery("protection_over_current", "UPS Over Current Protection", "problem",
                          "{{ 'true' if value_json.protection.over_current == true else 'false' }}");
    publishBinarySensorDiscovery("protection_over_temp", "UPS Over Temperature Protection", "problem",
                          "{{ 'true' if value_json.protection.over_temp == true else 'false' }}");
    publishBinarySensorDiscovery("protection_short_circuit", "UPS Short Circuit Protection", "problem",
                          "{{ 'true' if value_json.protection.short_circuit == true else 'false' }}");

    // 系统传感器
    publishSensorDiscovery("system_uptime", "UPS System Uptime", "duration", "measurement", "s",
                          "{{ value_json.system.uptime }}");
    publishSensorDiscovery("system_board_temperature", "UPS Board Temperature", "temperature", "measurement", "°C",
                          "{{ value_json.system.board_temperature }}");
    publishSensorDiscovery("system_environment_temperature", "UPS Environment Temperature", "temperature", "measurement", "°C",
                          "{{ value_json.system.environment_temperature }}");
    publishSensorDiscovery("system_board_temperature_sht", "UPS SHTC3 Temperature", "temperature", "measurement", "°C",
                          "{{ value_json.system.board_temperature_sht }}");
    publishSensorDiscovery("system_board_humidity", "UPS SHTC3 Humidity", "humidity", "measurement", "%",
                          "{{ value_json.system.board_humidity }}");
    publishSensorDiscovery("system_wifi_rssi", "UPS WiFi RSSI", "signal_strength", "measurement", "dBm",
                          "{{ value_json.system.wifi_rssi }}");
    publishSensorDiscovery("system_firmware_version", "UPS Firmware Version", nullptr, nullptr, nullptr,
                          "{{ value_json.system.firmware_version }}");
    publishSensorDiscovery("system_power_mode", "UPS Power Mode", nullptr, nullptr, nullptr,
                          "{{ value_json.system.power_mode }}");
    publishSensorDiscovery("system_overall_status", "UPS Overall Status", nullptr, nullptr, nullptr,
                          "{{ value_json.system.overall_status }}");
    publishSensorDiscovery("system_wifi_ssid", "UPS WiFi SSID", nullptr, nullptr, nullptr,
                          "{{ value_json.system.wifi_ssid }}");

    // 控制实体
    publishNumberDiscovery("led_brightness", "UPS LED Brightness",
                          "command/led_brightness/set", 0, 100, 1, "%",
                          "{{ value_json.config.led_brightness }}");
    publishNumberDiscovery("buzzer_volume", "UPS Buzzer Volume",
                          "command/buzzer_volume/set", 0, 100, 1, "%",
                          "{{ value_json.config.buzzer_volume }}");
    publishSelectDiscovery("hid_report_mode", "UPS HID Report Mode",
                          "command/hid_report_mode/set", "mAh,mWh,%",
                          "{{ value_json.config.hid_report_mode }}");
    return true;
}


// =============================================================================
// 传感器 Discovery
// =============================================================================
bool MQTTService::publishSensorDiscovery(const char* entity_id, const char* name,
                                         const char* device_class, const char* state_class,
                                         const char* unit, const char* value_template) {
    if (!m_connected) {
        return false;
    }

    StaticJsonDocument<512> doc;

    if (name) doc["name"] = name;
    if (device_class) doc["device_class"] = device_class;
    if (state_class) doc["state_class"] = state_class;
    if (unit) doc["unit_of_measurement"] = unit;
    doc["value_template"] = value_template ? value_template : "{{ value_json.value }}";

    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s", m_topic_base, TOPIC_STATE);
    doc["state_topic"] = state_topic;

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = unique_id;

    JsonObject device = doc.createNestedObject("device");
    setupDeviceInfo(device);
    JsonObject avail = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    avail["topic"] = availability_topic;
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", "homeassistant", m_device_id, entity_id);

    char buf[768];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len >= sizeof(buf) - 1) {
        return false;
    }
    return publishPayload(topic, (const uint8_t*)buf, len, 0, true);
}

bool MQTTService::publishBinarySensorDiscovery(const char* entity_id, const char* name,
                                               const char* device_class, const char* value_template,
                                               bool is_fault_sensor) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    if (device_class) doc["device_class"] = device_class;

    if (is_fault_sensor) {
        doc["payload_on"] = "fault";
        doc["payload_off"] = "None";
    } else {
        doc["payload_on"] = "true";
        doc["payload_off"] = "false";
    }
    doc["value_template"] = value_template ? value_template : "{{ value_json.value }}";

    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s", m_topic_base, TOPIC_STATE);
    doc["state_topic"] = state_topic;
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    return len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
}

bool MQTTService::publishSwitchDiscovery(const char* entity_id, const char* name,
                                         const char* command_topic,
                                         const char* value_template) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    char ct[96];
    snprintf(ct, sizeof(ct), "%s/%s", m_topic_base, command_topic);
    doc["command_topic"] = ct;
    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s", m_topic_base, TOPIC_STATE);
    doc["state_topic"] = state_topic;
    doc["payload_on"] = true; doc["payload_off"] = false; doc["retain"] = true;
    doc["value_template"] = value_template ? value_template : "{{ value_json.value }}";
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/switch/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    return len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
}

bool MQTTService::publishNumberDiscovery(const char* entity_id, const char* name,
                                         const char* command_topic,
                                         int min, int max, int step, const char* unit,
                                         const char* value_template) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    char ct[96];
    snprintf(ct, sizeof(ct), "%s/%s", m_topic_base, command_topic);
    doc["command_topic"] = ct;
    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s", m_topic_base, TOPIC_STATE);
    doc["state_topic"] = state_topic;
    doc["min"] = min; doc["max"] = max; doc["step"] = step;
    if (unit) doc["unit_of_measurement"] = unit;
    doc["retain"] = true;
    doc["value_template"] = value_template ? value_template : "{{ value_json.value }}";
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/number/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    return len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
}

bool MQTTService::publishSelectDiscovery(const char* entity_id, const char* name,
                                         const char* command_topic,
                                         const char* options,
                                         const char* value_template) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    char ct[96];
    snprintf(ct, sizeof(ct), "%s/%s", m_topic_base, command_topic);
    doc["command_topic"] = ct;
    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s", m_topic_base, TOPIC_STATE);
    doc["state_topic"] = state_topic;
    doc["retain"] = true;
    doc["value_template"] = value_template ? value_template : "{{ value_json.value }}";
    JsonArray opts = doc.createNestedArray("options");
    const char* p = options;
    while (p && *p) {
        const char* comma = strchr(p, ',');
        size_t olen = comma ? (comma - p) : strlen(p);
        char opt[16];
        if (olen >= sizeof(opt)) olen = sizeof(opt) - 1;
        strncpy(opt, p, olen); opt[olen] = '\0';
        opts.add(opt);
        p = comma ? comma + 1 : nullptr;
    }
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/select/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
    return result;
}

bool MQTTService::publishStateData() {
    if (!m_connected || !m_state) {
        return false;
    }

    // 构建合并 JSON 文档（~1.2KB）
    DynamicJsonDocument doc(1536);

    // BMS
    JsonObject bms = doc.createNestedObject("bms");
    bms["soc"] = m_state->bms.soc;
    bms["soh"] = m_state->bms.soh;
    bms["voltage"] = m_state->bms.voltage;
    bms["current"] = m_state->bms.current;
    bms["temperature"] = m_state->bms.temperature;
    bms["capacity_remaining"] = m_state->bms.capacity_remaining;
    bms["cycle_count"] = m_state->bms.cycle_count;
    bms["cell_1"] = m_state->bms.cell_voltages[0];
    bms["cell_2"] = m_state->bms.cell_voltages[1];
    bms["cell_3"] = m_state->bms.cell_voltages[2];
    bms["cell_4"] = m_state->bms.cell_voltages[3];
    bms["cell_5"] = m_state->bms.cell_voltages[4];
    bms["cell_min"] = m_state->bms.cell_voltage_min;
    bms["cell_max"] = m_state->bms.cell_voltage_max;
    bms["cell_avg"] = m_state->bms.cell_voltage_avg;
    bms["balancing_active"] = m_state->bms.balancing_active;
    bms["is_connected"] = m_state->bms.is_connected;
    bms["self_consumption"] = m_state->self_consumption_mA;
    bms["fault_type"] = getBmsFaultString(m_state->bms.fault_type);

    // 内阻数据
    JsonArray ir_arr = bms.createNestedArray("cell_ir");
    for (int i = 0; i < 5; i++) {
        ir_arr.add(serialized(String(m_state->bms.cell_internal_resistance[i], 1)));
    }
    bms["ir_sample_count"] = m_state->bms.ir_sample_count;

    // Power
    JsonObject power = doc.createNestedObject("power");
    power["input_voltage"] = m_state->power.input_voltage;
    power["input_current"] = m_state->power.input_current;
    power["output_power"] = m_state->power.output_power;
    power["battery_voltage"] = m_state->power.battery_voltage;
    power["battery_current"] = m_state->power.battery_current;
    power["ac_present"] = m_state->power.ac_present;
    power["charger_enabled"] = m_state->power.charger_enabled;
    power["fault_type"] = getPowerFaultString(m_state->power.fault_type);

    // System
    JsonObject system = doc.createNestedObject("system");
    system["uptime"] = m_state->system.uptime;
    system["board_temperature"] = m_state->system.board_temperature;
    system["environment_temperature"] = m_state->system.environment_temperature;
    system["board_temperature_sht"] = m_state->system.board_temperature_sht;
    system["board_humidity"] = m_state->system.board_humidity;
    system["wifi_connected"] = m_state->system.wifi_connected;
    system["wifi_rssi"] = m_state->system.wifi_rssi;
    system["wifi_ssid"] = m_state->system.wifi_ssid;
    system["firmware_version"] = m_state->system.firmware_version;
    system["power_mode"] = getPowerModeString(m_state->power_mode);
    system["overall_status"] = getOverallStatusString(m_state->overall_status);
    system["emergency_shutdown"] = m_state->emergency_shutdown;

    // Protection
    JsonObject prot = doc.createNestedObject("protection");
    prot["over_current"] = m_state->over_current_protection;
    prot["over_temp"] = m_state->over_temp_protection;
    prot["short_circuit"] = m_state->short_circuit_protection;

    // Config
    if (m_config) {
        JsonObject cfg = doc.createNestedObject("config");
        cfg["led_brightness"] = m_config->led_brightness;
        cfg["buzzer_enabled"] = m_config->buzzer_enabled;
        cfg["buzzer_volume"] = m_config->buzzer_volume;
        cfg["hid_report_mode"] = getHidReportModeString(m_config->hid_report_mode);
    }

    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, TOPIC_STATE);
    char buf[1536];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len >= sizeof(buf) - 1) {
        DBG.println("[MQTT] State JSON overflow, skipped");
        return false;
    }
    return publishPayload(topic, (const uint8_t*)buf, len, 0, false);
}

bool MQTTService::publishAvailability(bool online) {
    if (!m_connected) {
        return false;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    const char* payload = online ? "online" : "offline";
    bool result = publishPayload(topic, (const uint8_t*)payload, strlen(payload), 0, true);
    return result;
}

bool MQTTService::checkWifiConnected() { return WiFi.isConnected() && WiFi.status() == WL_CONNECTED; }

/**
 * @brief 设置 Home Assistant 设备信息
 * @param device JsonObject 引用，用于填充设备信息
 */
void MQTTService::setupDeviceInfo(JsonObject device) {
    device["identifiers"][0] = m_device_id;
    device["name"] = "UPS";
    device["manufacturer"] = "OpenUPS";
    device["model"] = "ESP32-S3";
    device["sw_version"] = FIRMWARE_ID_TAG;
    setupDeviceConfigUrl(device);
}

void MQTTService::setupDeviceConfigUrl(JsonObject device) {
    char config_url[32];
    snprintf(config_url, sizeof(config_url), "http://%s/", WiFi.localIP().toString().c_str());
    device["configuration_url"] = config_url;
}

void MQTTService::handleCommand(const char* topic, const char* payload, unsigned int length) {
    char* cmd = strstr(topic, "/command/");
    if (!cmd) return;
    cmd += 9;
    char* end = strstr(cmd, "/set");
    if (!end) return;
    *end = '\0';
    char payload_str[64];
    strncpy(payload_str, payload, length < 63 ? length : 63);
    payload_str[length < 63 ? length : 63] = '\0';
    DBG.printf("[MQTT] Cmd: %s = %s\n", cmd, payload_str);
    
    // 检查配置指针有效性
    if (!m_config) {
        DBG.println("[MQTT] Config not available, command ignored");
        return;
    }
    
    // 创建配置副本（所有命令共用）
    Configuration config_copy;
    memcpy(&config_copy, m_config, sizeof(Configuration));
    
    bool config_changed = false;
    
    // 根据命令类型处理
    if (strcmp(cmd, "hid_report_mode") == 0) {
        uint8_t new_mode = 0;
        
        // 将字符串值转换为数字索引
        if (strcmp(payload_str, "mAh") == 0) {
            new_mode = 0;
        } else if (strcmp(payload_str, "mWh") == 0) {
            new_mode = 1;
        } else if (strcmp(payload_str, "%") == 0) {
            new_mode = 2;
        } else {
            // 尝试解析为数字（向后兼容）
            new_mode = (uint8_t)atoi(payload_str);
            if (new_mode > 2) {
                DBG.printf("[MQTT] Invalid hid_report_mode value: %s\n", payload_str);
                return;
            }
        }
        
        // 检查是否需要更新
        if (m_config->hid_report_mode != new_mode) {
            config_copy.hid_report_mode = new_mode;
            config_changed = true;
            DBG.printf("[MQTT] hid_report_mode change requested: %d (%s)\n", new_mode, payload_str);
        }
    }
    else if (strcmp(cmd, "buzzer_volume") == 0) {
        uint8_t new_volume = (uint8_t)atoi(payload_str);
        
        // 验证范围 (0-100)
        if (new_volume > 100) {
            DBG.printf("[MQTT] Invalid buzzer_volume value: %s (must be 0-100)\n", payload_str);
            return;
        }
        
        // 检查是否需要更新
        if (m_config->buzzer_volume != new_volume) {
            config_copy.buzzer_volume = new_volume;
            config_changed = true;
            DBG.printf("[MQTT] buzzer_volume change requested: %d\n", new_volume);
        }
    }
    else if (strcmp(cmd, "led_brightness") == 0) {
        uint8_t new_brightness = (uint8_t)atoi(payload_str);
        
        // 验证范围 (0-100)
        if (new_brightness > 100) {
            DBG.printf("[MQTT] Invalid led_brightness value: %s (must be 0-100)\n", payload_str);
            return;
        }
        
        // 检查是否需要更新
        if (m_config->led_brightness != new_brightness) {
            config_copy.led_brightness = new_brightness;
            config_changed = true;
            DBG.printf("[MQTT] led_brightness change requested: %d\n", new_brightness);
        }
    }
    else {
        // 未知命令
        DBG.printf("[MQTT] Unknown command: %s\n", cmd);
        return;
    }
    
    // 如果配置有变更，发布事件
    if (config_changed) {
        EventBus::getInstance().publish(EVT_CONFIG_SYSTEM_CHANGE_REQUEST, &config_copy);
    }
}

// =============================================================================
// 辅助方法实现
// =============================================================================

const char* MQTTService::getHidReportModeString(uint8_t mode) const {
    switch (mode) {
        case 0: return "mAh";
        case 1: return "mWh";
        case 2: return "%";
        default: return "unknown";
    }
}

const char* MQTTService::getBmsFaultString(uint8_t fault) const {
    switch (fault) {
        case BMS_FAULT_NONE: return "None";
        case BMS_FAULT_OVER_VOLTAGE: return "Over Voltage";
        case BMS_FAULT_UNDER_VOLTAGE: return "Under Voltage";
        case BMS_FAULT_OVER_CURRENT: return "Over Current";
        case BMS_FAULT_SHORT_CIRCUIT: return "Short Circuit";
        case BMS_FAULT_OVER_TEMP: return "Over Temperature";
        case BMS_FAULT_CHIP_ERROR: return "Chip Error";
        case BMS_FAULT_PASSIVE_SHUTDOWN: return "Passive Shutdown";
        default: return "Unknown";
    }
}

const char* MQTTService::getPowerFaultString(uint8_t fault) const {
    switch (fault) {
        case POWER_FAULT_NONE: return "None";
        case POWER_FAULT_CHIP_ERROR: return "Chip Error";
        case POWER_FAULT_OVER_CURRENT: return "Over Current";
        case POWER_FAULT_OVER_TEMPERATURE: return "Over Temperature";
        case POWER_FAULT_INPUT_OVERVOLTAGE: return "Input Overvoltage";
        case POWER_FAULT_INPUT_UNDERVOLTAGE: return "Input Undervoltage";
        case POWER_FAULT_BATTERY_OVERVOLTAGE: return "Battery Overvoltage";
        case POWER_FAULT_BATTERY_UNDERVOLTAGE: return "Battery Undervoltage";
        case POWER_FAULT_SHORT_CIRCUIT: return "Short Circuit";
        case POWER_FAULT_CHARGE_TIMEOUT: return "Charge Timeout";
        case POWER_FAULT_I2C_COMMUNICATION: return "I2C Communication Error";
        default: return "Unknown";
    }
}

const char* MQTTService::getPowerModeString(uint8_t mode) const {
    switch (mode) {
        case POWER_MODE_AC: return "AC";
        case POWER_MODE_BATTERY: return "Battery";
        case POWER_MODE_HYBRID: return "Hybrid";
        case POWER_MODE_CHARGING: return "Charging";
        default: return "Unknown";
    }
}

const char* MQTTService::getOverallStatusString(uint8_t status) const {
    switch (status) {
        case OVERALL_STATUS_NORMAL: return "Normal";
        case OVERALL_STATUS_WARNING: return "Warning";
        case OVERALL_STATUS_FAULT: return "Fault";
        default: return "Unknown";
    }
}
