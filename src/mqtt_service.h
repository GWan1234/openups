#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <Arduino.h>
#include "ArduinoJson.h"
#include "data_structures.h"

// 前向声明 - 避免包含 AsyncMQTT_ESP32.h 导致链接错误
// AsyncMqttClient 的具体实现在 mqtt_service.cpp 中
class AsyncMqttClient;

#define MQTT_KEEPALIVE 60
static const char* TOPIC_AVAILABILITY = "availability";
static const char* TOPIC_STATE = "state";

// =============================================================================
// MQTT 服务类
// =============================================================================
class MQTTService {
public:
    // 构造函数
    MQTTService();
    ~MQTTService();

    // 初始化
    bool begin(Configuration* config, System_Global_State* state);

    // 主循环调用
    void loop(System_Global_State& state);

    // 连接状态
    bool isConnected();
    bool isConfigured() const { return m_configured; }

    // 连接 MQTT Broker
    bool connect();

    // 断开连接
    void disconnect();

    // 发布 Discovery 配置
    bool publishDiscoveryConfig();

    // 发布状态数据
    bool publishStateData();

    // 设置设备标识符
    void setDeviceIdentifier(const char* identifier);

    // 设置 MQTT Broker 地址
    void setBrokerAddress(const char* broker);

    // 设置 MQTT 端口 (自动检测 TLS: 8883 启用 TLS)
    void setBrokerPort(uint16_t port);

    // 设置 MQTT 凭证
    void setBrokerCredentials(const char* username, const char* password);

    // 设置静态回调实例指针（在 begin 中调用）
    static void setStaticInstance(MQTTService* instance);

    // 传感器 Discovery
    bool publishSensorDiscovery(const char* entity_id, const char* name,
                                const char* device_class, const char* state_class,
                                const char* unit, const char* value_template);

    // 二进制传感器 Discovery
    bool publishBinarySensorDiscovery(const char* entity_id, const char* name,
                                      const char* device_class, const char* value_template,
                                      bool is_fault_sensor = false);

    // 开关 Discovery
    bool publishSwitchDiscovery(const char* entity_id, const char* name,
                                const char* command_topic,
                                const char* value_template = nullptr);

    // 数值控制 Discovery
    bool publishNumberDiscovery(const char* entity_id, const char* name,
                                const char* command_topic,
                                int min, int max, int step, const char* unit,
                                const char* value_template = nullptr);

    // 选择控制 Discovery
    bool publishSelectDiscovery(const char* entity_id, const char* name,
                                const char* command_topic,
                                const char* options,
                                const char* value_template = nullptr);

private:
    // 静态实例指针（用于静态回调）
    static MQTTService* s_instance;
    
    /**
     * @brief 设置 Home Assistant 设备信息
     * @param device JsonObject 引用，用于填充设备信息
     */
    void setupDeviceInfo(JsonObject device);
    
    // 配置数据
    Configuration* m_config;
    System_Global_State* m_state;
    AsyncMqttClient* m_mqtt_client;  // 前向声明指针
    bool m_use_tls;
    bool m_configured;
    bool m_connected;
    bool m_discovery_published;
    char m_device_id[32];
    char m_topic_base[64];
    unsigned long m_last_state_publish;
    unsigned long m_last_heartbeat;
    unsigned long m_last_reconnect_attempt;
    unsigned long m_connected_since;
    unsigned long m_last_puback;
    uint8_t m_consecutive_failures;
    char m_mqtt_broker[64];
    uint16_t m_mqtt_port;
    char m_mqtt_username[64];
    char m_mqtt_password[64];

    // =============================================================================
    // 私有方法
    // =============================================================================

    // AsyncMqttClient 回调函数
    static void onMqttConnect(bool sessionPresent);
    static void onMqttDisconnect(int reason);
    static void onMqttMessage(char* topic, char* payload, int properties, size_t len, size_t index, size_t total);
    static void onMqttPublish(uint16_t packetId);
    void handleCommand(const char* topic, const char* payload, unsigned int length);
    void subscribeCommandTopics();

    // 发布辅助
    bool publishPayload(const char* topic, const uint8_t* payload, size_t length, uint8_t qos, bool retain);

    bool publishAvailability(bool online);
    bool checkWifiConnected();

    // 设置设备的 configuration_url
    void setupDeviceConfigUrl(JsonObject device);
    
    // 辅助方法：将数字值转换为可读字符串
    const char* getHidReportModeString(uint8_t mode) const;
    const char* getBmsFaultString(uint8_t fault) const;
    const char* getPowerFaultString(uint8_t fault) const;
    const char* getPowerModeString(uint8_t mode) const;
    const char* getOverallStatusString(uint8_t status) const;
};

#endif // MQTT_SERVICE_H