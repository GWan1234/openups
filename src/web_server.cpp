#include "web_server.h"
#include "data_structures.h"
#include "config_manager.h"
#include "templates/html_templates.h"
#include "system_management.h"
#include "event_bus.h"
#include "event_types.h"
#include "i18n.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "debug.h"
#include <esp_ota_ops.h>
#include <Ticker.h>
#include <SPIFFS.h>


// OTA 配置 —— 修改项目名或最低版本时只需改这里
#define EXPECTED_SIG_PREFIX   "SIG:OPENUPS-ESP32S3:VER:"
#define MIN_REQUIRED_VERSION  "1.0.0"

// OTA 结果跟踪标志 (upload handler 与 request handler 共享)
static bool s_otaSuccess = false;

extern bool g_is_new_board;

// =============================================================================
// Constructor and Destructor
// =============================================================================

WebServer::WebServer(ConfigManager* configMgr, SystemManagement* sysMgr, int port)
    : server(port), ws("/ws"), configManager(configMgr), systemManager(sysMgr),
      login_failures_(0), login_lockout_until_(0) {
  memset(sessions_, 0, sizeof(sessions_));

  if (systemManager == nullptr) {
    DBG.println(F("WebServer: CONFIG_MODE (no systemManager)"));
  } else {
    DBG.println(F("WebServer: NORMAL_MODE"));
  }
}

WebServer::~WebServer() {
  DBG.println(F("WebServer: Destructor called"));
}

// =============================================================================
// Server Setup
// =============================================================================

// 请求体分块收集器：累积到 request->_tempObject（String*），由 handler 取走并释放
static void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
  if (!request->_tempObject) {
    request->_tempObject = new String();
    ((String*)request->_tempObject)->reserve(512);
  }
  String* body = (String*)request->_tempObject;
  for (size_t i = 0; i < len; i++) {
    body->concat((char)data[i]);
  }
}

// 丢弃请求体（早返回时释放 _tempObject）
static void discardBody(AsyncWebServerRequest* request) {
  delete (String*)request->_tempObject;
  request->_tempObject = nullptr;
}

// 取走请求体（转移所有权），无 body 返回 false
static bool takeBody(AsyncWebServerRequest* request, String& out) {
  if (!request->_tempObject) return false;
  out = *(String*)request->_tempObject;
  discardBody(request);
  return true;
}

// 认证 + 无请求体的路由
void WebServer::onAuth(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler) {
  server.on(uri, method, [this, handler](AsyncWebServerRequest* request) {
    if (!ensureAuthenticated(request)) return;
    handler(request);
  });
}

// POST + 请求体收集的路由（无认证，供登录/首次设置使用）
void WebServer::onBody(const char* uri, ArRequestHandlerFunction handler) {
  server.on(uri, HTTP_POST, handler, NULL,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
      collectBody(request, data, len);
    });
}

// 认证 + POST + 请求体收集的路由
void WebServer::onAuthBody(const char* uri, ArRequestHandlerFunction handler) {
  onBody(uri, [this, handler](AsyncWebServerRequest* request) {
    if (!ensureAuthenticated(request)) {
      discardBody(request);
      return;
    }
    handler(request);
  });
}

bool WebServer::begin() {
  ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                    AwsEventType type, void* arg, uint8_t* data, size_t len) {
    this->onWsEvent(server, client, type, arg, data, len);
  });
  
  server.addHandler(&ws);
  setupHttpRoutes();
  server.begin();
  
  DBG.println(F("Web server started on port 80"));
  return true;
}

void WebServer::setupHttpRoutes() {
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleRoot(request);
  });

  // /config and /update redirect to SPA root
  auto redirectToRoot = [](AsyncWebServerRequest* request) {
    request->redirect("/");
  };
  server.on("/config", HTTP_GET, redirectToRoot);
  server.on("/update", HTTP_GET, redirectToRoot);

  // ---- 认证接口（login/setup 本身无需认证）----
  onBody("/api/auth/login", [this](AsyncWebServerRequest* r) { handleAuthLogin(r); });
  onBody("/api/auth/setup", [this](AsyncWebServerRequest* r) { handleAuthSetup(r); });
  onAuthBody("/api/auth/change", [this](AsyncWebServerRequest* r) { handleAuthChange(r); });
  server.on("/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest* r) { handleAuthLogout(r); });

  // ---- 配置与控制接口（认证）----
  onAuthBody("/save", [this](AsyncWebServerRequest* r) { handleSaveConfig(r); });
  onAuth("/bms/shipmode", HTTP_POST, [this](AsyncWebServerRequest* r) { handleBmsShipMode(r); });
  onAuth("/bms/reset-data", HTTP_POST, [this](AsyncWebServerRequest* r) { handleBmsResetData(r); });
  onAuth("/api/clear-tips", HTTP_POST, [this](AsyncWebServerRequest* r) { handleClearTips(r); });
  onAuth("/api/restart", HTTP_POST, [this](AsyncWebServerRequest* r) { handleRestart(r); });
  onAuth("/api/calibration", HTTP_GET, [this](AsyncWebServerRequest* r) { handleCalibrationGet(r); });
  onAuthBody("/api/calibration", [this](AsyncWebServerRequest* r) { handleCalibrationPost(r); });
  onAuthBody("/api/set-lang", [this](AsyncWebServerRequest* r) { handleSetLang(r); });

  // ---- 状态查询接口（认证）----
  static const char* const apiRoutes[] = {"/api/status", "/api/bms", "/api/power"};
  for (const char* route : apiRoutes) {
    onAuth(route, HTTP_GET, [this, route](AsyncWebServerRequest* r) { handleApiRequest(r, route); });
  }

  // ---- 文件下载接口（认证）----
  onAuth("/api/raw-files", HTTP_GET, [this](AsyncWebServerRequest* r) { handleFileList(r, "/raw"); });
  onAuth("/api/raw-file", HTTP_GET, [this](AsyncWebServerRequest* r) { handleFileDownload(r, "/raw/"); });
  onAuth("/api/log-files", HTTP_GET, [this](AsyncWebServerRequest* r) { handleFileList(r, "/log"); });
  onAuth("/api/log-file", HTTP_GET, [this](AsyncWebServerRequest* r) { handleFileDownload(r, "/log/"); });

  // Prometheus metrics - 按需求豁免认证
  server.on("/metrics", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (systemManager == nullptr) {
      sendConfigModeResponse(request);
      return;
    }
    handleMetricsRequest(request);
  });

  // OTA 固件上传（认证在 handleFirmwareUpload 内进行，须先于 flash 写入）
  server.on("/firmware", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      if (!ensureAuthenticated(request)) return;
      this->handleFirmwareResult(request);
    },
    [this](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      this->handleFirmwareUpload(request, filename, index, data, len, final);
    }
  );

  DBG.println(systemManager ? F("API routes: NORMAL_MODE") : F("API routes: CONFIG_MODE (stubs)"));
}

// OTA 上传完成响应 — 仅根据 s_otaSuccess 判断是否真正写入成功
void WebServer::handleFirmwareResult(AsyncWebServerRequest* request) {
  DBG.printf_P(PSTR("[OTA-REQ] s_otaSuccess=%d hasError=%d\n"), s_otaSuccess, Update.hasError());

  if (!s_otaSuccess || Update.hasError()) {
    String message = Update.hasError() ? Update.errorString() : "OTA verification or write failed";
    sendErrorResponse(request, message, 500);
    DBG.println(F("OTA: 升级失败，固件未写入。"));
    return;
  }

  request->send(200, "application/json",
    "{\"success\":true,\"message\":\"Firmware update successful. Device will reboot in 3 seconds...\"}");

  DBG.println(F("==========================================="));
  DBG.println(F("OTA update successful! Rebooting in 3 seconds..."));
  DBG.println(F("==========================================="));

  static Ticker rebootTicker;
  rebootTicker.once(3, []() {
    DBG.println(F("Rebooting now..."));
    ESP.restart();
  });
}

// 语言切换（原内联在路由注册中）
void WebServer::handleSetLang(AsyncWebServerRequest* request) {
  String body;
  if (!takeBody(request, body)) {
    sendErrorResponse(request, "Missing request body", 400);
    return;
  }

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, body)) {
    request->send(400, "application/json", "{\"success\":false}");
    return;
  }

  const char* lang = doc["lang"];
  I18n::setLanguage((lang && strcmp(lang, "en") == 0) ? LANG_EN : LANG_ZH);

  char buf[96];
  snprintf(buf, sizeof(buf), "{\"success\":true,\"lang\":\"%s\"}", I18n::getLangCode());
  request->send(200, "application/json", buf);
}

// =============================================================================
// WebSocket Event Handling
// =============================================================================

void WebServer::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                          AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    if (systemManager != nullptr) {
      notifyClients();
    } else {
      StaticJsonDocument<256> doc;
      doc["status"] = "config_mode";
      doc["message"] = "Hardware modules not initialized";
      doc["timestamp"] = millis();
      
      char buffer[512];
      serializeJson(doc, buffer);
      client->text(buffer);
    }
  }else if (type == WS_EVT_DATA) {
    // 处理客户端消息
    String msg = "";
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }
    if (msg == "get" && systemManager != nullptr) {
      notifyClients();
    }
  }
}

// =============================================================================
// Client Notification
// =============================================================================

void WebServer::notifyClients() {
  if (systemManager == nullptr) {
    StaticJsonDocument<256> doc;
    doc["status"] = "config_mode";
    doc["message"] = "Hardware modules not initialized";
    doc["timestamp"] = millis();
    
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);
    ws.textAll(jsonBuffer);
    return;
  }
  
  if (ws.count() == 0) return;
  
  const System_Global_State& state = systemManager->getGlobalState();
  DynamicJsonDocument doc(3584);

  doc["status"] = "connected";
  doc["timestamp"] = millis();
  doc["overall_status"] = state.overall_status;
  doc["power_mode"] = state.power_mode;
  doc["emergency_shutdown"] = state.emergency_shutdown;

  // Tips
  if (state.tip_count > 0) {
    JsonArray tips = doc.createNestedArray("tips");
    uint8_t count = state.tip_count < SYSTEM_TIPS_MAX ? state.tip_count : SYSTEM_TIPS_MAX;
    for (uint8_t i = 0; i < count; i++) {
      uint8_t idx = (state.tip_index + SYSTEM_TIPS_MAX - count + i) % SYSTEM_TIPS_MAX;
      if (state.tips[idx].message[0] != '\0') {
        JsonObject t = tips.createNestedObject();
        t["msg"] = state.tips[idx].message;
      }
    }
  }

  // BMS data - 完整数据
  JsonObject bms = doc.createNestedObject("bms");
  bms["soc"] = state.bms.soc;
  bms["soh"] = state.bms.soh;
  bms["voltage"] = state.bms.voltage;
  bms["current"] = state.bms.current;
  bms["temperature"] = state.bms.temperature;
  bms["cycle_count"] = state.bms.cycle_count;
  bms["capacity_remaining"] = state.bms.capacity_remaining;
  bms["is_connected"] = state.bms.is_connected;
  bms["bms_mode"] = state.bms.bms_mode;
  bms["fault_type"] = state.bms.fault_type;
  bms["balancing_active"] = state.bms.balancing_active;
  bms["balance_mask"] = state.bms.balance_mask;

  // 均衡统计数据
  bms["balancing_events_total"] = state.bms.balancing_events_total;
  JsonArray cellBalCounts = bms.createNestedArray("cell_balancing_count");
  for (int i = 0; i < 5; i++) {
    cellBalCounts.add(state.bms.cell_balancing_count[i]);
  }

  // 单体电压数组
  JsonArray cells = bms.createNestedArray("cell_voltages");
  for (int i = 0; i < 5; i++) {
    cells.add(state.bms.cell_voltages[i]);
  }
  bms["cell_voltage_min"] = state.bms.cell_voltage_min;
  bms["cell_voltage_max"] = state.bms.cell_voltage_max;
  bms["cell_voltage_avg"] = state.bms.cell_voltage_avg;

  // 内阻估算数组
  JsonArray ir_arr = bms.createNestedArray("cell_ir");
  for (int i = 0; i < 5; i++) {
    ir_arr.add(serialized(String(state.bms.cell_internal_resistance[i], 1)));
  }
  bms["ir_sample_count"] = state.bms.ir_sample_count;

  // BQ76920 寄存器数组
  JsonArray bq76920_regs = bms.createNestedArray("bq76920_registers");
  for (int i = 0; i < 12; i++) {
    bq76920_regs.add(state.bms.bq76920_registers[i]);
  }

  // Power data - 完整数据
  JsonObject power = doc.createNestedObject("power");
  power["input_voltage"] = state.power.input_voltage;
  power["input_current"] = state.power.input_current;
  power["output_power"] = state.power.output_power;
  power["battery_voltage"] = state.power.battery_voltage;
  power["battery_current"] = state.power.battery_current;
  power["ac_present"] = state.power.ac_present;
  power["charger_enabled"] = state.power.charger_enabled;
  power["hybrid_mode"] = state.power.hybrid_mode;
  power["fault_type"] = state.power.fault_type;
  power["bq24780s_connected"] = state.power.bq24780s_connected;
  power["prochot_status"] = state.power.prochot_status;
  power["tbstat_status"] = state.power.tbstat_status;
  power["chip_variant"] = state.power.chip_variant;

  // BQ24780S/BQ24800 寄存器数组 (12个：前11个通用 + [11]VsysMin仅BQ24800)
  JsonArray bq24780s_regs = power.createNestedArray("bq24780s_registers");
  for (int i = 0; i < 12; i++) {
    bq24780s_regs.add(state.power.bq24780s_registers[i]);
  }

  // System data - 完整数据
  JsonObject system = doc.createNestedObject("system");
  system["uptime"] = state.system.uptime;
  system["wifi_connected"] = state.system.wifi_connected;
  system["wifi_ssid"] = state.system.wifi_ssid;
  system["wifi_rssi"] = state.system.wifi_rssi;
  system["board_temperature"] = state.system.board_temperature;
  system["environment_temperature"] = state.system.environment_temperature;
  system["board_temperature_sht"] = state.system.board_temperature_sht;
  system["board_humidity"] = state.system.board_humidity;
  system["firmware_version"] = state.system.firmware_version;

  // 自消耗计算数据
  doc["self_consumption_mA"] = serialized(String(state.self_consumption_mA, 2));
  doc["sc_last_update"] = state.sc_last_update;

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// =============================================================================
// API Endpoints
// =============================================================================

void WebServer::handleApiRequest(AsyncWebServerRequest* request, const char* route) {
  if (systemManager == nullptr) {
    request->send(200, "application/json", "{\"status\":\"config_mode\",\"message\":\"Hardware modules not initialized\"}");
    return;
  }

  const System_Global_State& state = systemManager->getGlobalState();
  DynamicJsonDocument doc(1024);
  
  if (strcmp(route, "/api/status") == 0) {
    buildStatusResponse(doc, state);
  } else if (strcmp(route, "/api/bms") == 0) {
    buildBmsResponse(doc, state);
  } else if (strcmp(route, "/api/power") == 0) {
    buildPowerResponse(doc, state);
  }
  
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServer::buildStatusResponse(DynamicJsonDocument& doc, const System_Global_State& state) {
  doc["timestamp"] = millis();
  doc["uptime"] = state.system.uptime;
  doc["overall_status"] = state.overall_status;
  doc["power_mode"] = state.power_mode;
  doc["emergency_shutdown"] = state.emergency_shutdown;
  
  JsonObject bms = doc.createNestedObject("bms");
  bms["soc"] = state.bms.soc;
  bms["voltage"] = state.bms.voltage;
  bms["current"] = state.bms.current;
  bms["temperature"] = state.bms.temperature;
  bms["is_connected"] = state.bms.is_connected;
  
  JsonObject power = doc.createNestedObject("power");
  power["ac_present"] = state.power.ac_present;
  power["charger_enabled"] = state.power.charger_enabled;
  power["battery_voltage"] = state.power.battery_voltage;
  power["hybrid_mode"] = state.power.hybrid_mode;
  
  JsonObject system = doc.createNestedObject("system");
  system["wifi_connected"] = state.system.wifi_connected;
  system["wifi_rssi"] = state.system.wifi_rssi;
  system["board_temperature"] = state.system.board_temperature;
  system["board_temperature_sht"] = state.system.board_temperature_sht;
  system["board_humidity"] = state.system.board_humidity;

  // 自消耗计算
  doc["self_consumption_mA"] = state.self_consumption_mA;
  doc["sc_last_update"] = state.sc_last_update;
}

void WebServer::buildBmsResponse(DynamicJsonDocument& doc, const System_Global_State& state) {
  doc["timestamp"] = state.bms.last_update_time;
  doc["soc"] = state.bms.soc;
  doc["soh"] = state.bms.soh;
  doc["voltage"] = state.bms.voltage;
  doc["current"] = state.bms.current;
  doc["temperature"] = state.bms.temperature;
  doc["is_connected"] = state.bms.is_connected;
  doc["bms_mode"] = state.bms.bms_mode;
  doc["fault_type"] = state.bms.fault_type;
  doc["cycle_count"] = state.bms.cycle_count;
  doc["capacity_remaining"] = state.bms.capacity_remaining;
  doc["balancing_active"] = state.bms.balancing_active;
  doc["balance_mask"] = state.bms.balance_mask;
  
  JsonArray cells = doc.createNestedArray("cell_voltages");
  for (int i = 0; i < 5; i++) {
    cells.add(state.bms.cell_voltages[i]);
  }
  doc["cell_voltage_min"] = state.bms.cell_voltage_min;
  doc["cell_voltage_max"] = state.bms.cell_voltage_max;
  doc["cell_voltage_avg"] = state.bms.cell_voltage_avg;

  JsonArray ir_arr = doc.createNestedArray("cell_ir");
  for (int i = 0; i < 5; i++) {
    ir_arr.add(serialized(String(state.bms.cell_internal_resistance[i], 1)));
  }
  doc["ir_sample_count"] = state.bms.ir_sample_count;
}

void WebServer::buildPowerResponse(DynamicJsonDocument& doc, const System_Global_State& state) {
  doc["timestamp"] = state.power.last_update_time;
  
  JsonObject input = doc.createNestedObject("input");
  input["voltage"] = state.power.input_voltage;
  input["current"] = state.power.input_current;
  
  JsonObject output = doc.createNestedObject("output");
  output["power"] = state.power.output_power;
  
  JsonObject battery = doc.createNestedObject("battery");
  battery["voltage"] = state.power.battery_voltage;
  battery["current"] = state.power.battery_current;
  
  doc["ac_present"] = state.power.ac_present;
  doc["charger_enabled"] = state.power.charger_enabled;
  doc["hybrid_mode"] = state.power.hybrid_mode;
  doc["fault_type"] = state.power.fault_type;
  doc["prochot_status"] = state.power.prochot_status;
  doc["tbstat_status"] = state.power.tbstat_status;
}

// =============================================================================
// Prometheus Metrics Endpoint
// =============================================================================

void WebServer::handleMetricsRequest(AsyncWebServerRequest* request) {
  if (systemManager == nullptr) {
    request->send(200, "text/plain", "# Prometheus metrics not available in config mode\n");
    return;
  }

  const System_Global_State& state = systemManager->getGlobalState();
  
  // Build Prometheus text format response
  String metrics = "";
  
  // System metrics
  metrics += "# HELP ups_system_uptime System uptime in seconds\n";
  metrics += "# TYPE ups_system_uptime gauge\n";
  metrics += "ups_system_uptime " + String(state.system.uptime) + "\n\n";
  
  metrics += "# HELP ups_system_overall_status Overall system status (0=normal, 1=warning, 2=fault)\n";
  metrics += "# TYPE ups_system_overall_status gauge\n";
  metrics += "ups_system_overall_status " + String(state.overall_status) + "\n\n";
  
  metrics += "# HELP ups_system_power_mode Power mode (0=AC, 1=BATTERY, 2=HYBRID, 3=CHARGING)\n";
  metrics += "# TYPE ups_system_power_mode gauge\n";
  metrics += "ups_system_power_mode " + String(state.power_mode) + "\n\n";
  
  metrics += "# HELP ups_system_emergency_shutdown Emergency shutdown status (0=normal, 1=shutdown)\n";
  metrics += "# TYPE ups_system_emergency_shutdown gauge\n";
  metrics += "ups_system_emergency_shutdown " + String(state.emergency_shutdown ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_system_board_temperature Board temperature in Celsius\n";
  metrics += "# TYPE ups_system_board_temperature gauge\n";
  metrics += "ups_system_board_temperature " + String(state.system.board_temperature, 2) + "\n\n";
  
  metrics += "# HELP ups_system_environment_temperature Environment temperature in Celsius\n";
  metrics += "# TYPE ups_system_environment_temperature gauge\n";
  metrics += "ups_system_environment_temperature " + String(state.system.environment_temperature, 2) + "\n\n";

  metrics += "# HELP ups_system_board_temperature_sht SHTC3 board temperature in Celsius\n";
  metrics += "# TYPE ups_system_board_temperature_sht gauge\n";
  metrics += "ups_system_board_temperature_sht " + String(state.system.board_temperature_sht, 2) + "\n\n";

  metrics += "# HELP ups_system_board_humidity SHTC3 board humidity in percent\n";
  metrics += "# TYPE ups_system_board_humidity gauge\n";
  metrics += "ups_system_board_humidity " + String(state.system.board_humidity, 2) + "\n\n";
  
  metrics += "# HELP ups_system_wifi_connected WiFi connection status (0=disconnected, 1=connected)\n";
  metrics += "# TYPE ups_system_wifi_connected gauge\n";
  metrics += "ups_system_wifi_connected " + String(state.system.wifi_connected ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_system_wifi_rssi WiFi signal strength in dBm\n";
  metrics += "# TYPE ups_system_wifi_rssi gauge\n";
  metrics += "ups_system_wifi_rssi " + String(state.system.wifi_rssi) + "\n\n";
  
  metrics += "# HELP ups_system_wifi_status WiFi status code\n";
  metrics += "# TYPE ups_system_wifi_status gauge\n";
  metrics += "ups_system_wifi_status " + String(state.system.wifi_status) + "\n\n";
  
  metrics += "# HELP ups_system_led_brightness LED brightness (0-255)\n";
  metrics += "# TYPE ups_system_led_brightness gauge\n";
  metrics += "ups_system_led_brightness " + String(state.system.led_brightness) + "\n\n";
  
  metrics += "# HELP ups_system_buzzer_enabled Buzzer enabled status (0=disabled, 1=enabled)\n";
  metrics += "# TYPE ups_system_buzzer_enabled gauge\n";
  metrics += "ups_system_buzzer_enabled " + String(state.system.buzzer_enabled ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_system_buzzer_volume Buzzer volume (0-255)\n";
  metrics += "# TYPE ups_system_buzzer_volume gauge\n";
  metrics += "ups_system_buzzer_volume " + String(state.system.buzzer_volume) + "\n\n";
  
  metrics += "# HELP ups_system_hardware_version Hardware version\n";
  metrics += "# TYPE ups_system_hardware_version gauge\n";
  metrics += "ups_system_hardware_version{version=\"" + String(state.system.hardware_version) + "\"} 1\n\n";
  
  // BMS metrics
  metrics += "# HELP ups_bms_soc Battery state of charge percentage\n";
  metrics += "# TYPE ups_bms_soc gauge\n";
  metrics += "ups_bms_soc " + String(state.bms.soc, 2) + "\n\n";
  
  metrics += "# HELP ups_bms_soh Battery state of health percentage\n";
  metrics += "# TYPE ups_bms_soh gauge\n";
  metrics += "ups_bms_soh " + String(state.bms.soh, 2) + "\n\n";
  
  metrics += "# HELP ups_bms_voltage Battery voltage in millivolts\n";
  metrics += "# TYPE ups_bms_voltage gauge\n";
  metrics += "ups_bms_voltage " + String(state.bms.voltage) + "\n\n";
  
  metrics += "# HELP ups_bms_current Battery current in milliamperes (positive=charging, negative=discharging)\n";
  metrics += "# TYPE ups_bms_current gauge\n";
  metrics += "ups_bms_current " + String(state.bms.current) + "\n\n";
  
  metrics += "# HELP ups_bms_temperature Battery temperature in Celsius\n";
  metrics += "# TYPE ups_bms_temperature gauge\n";
  metrics += "ups_bms_temperature " + String(state.bms.temperature, 2) + "\n\n";
  
  metrics += "# HELP ups_bms_cycle_count Battery cycle count\n";
  metrics += "# TYPE ups_bms_cycle_count gauge\n";
  metrics += "ups_bms_cycle_count " + String(state.bms.cycle_count) + "\n\n";
  
  metrics += "# HELP ups_bms_capacity_full Full battery capacity in mAh\n";
  metrics += "# TYPE ups_bms_capacity_full gauge\n";
  metrics += "ups_bms_capacity_full " + String(state.bms.capacity_full) + "\n\n";
  
  metrics += "# HELP ups_bms_capacity_remaining Remaining battery capacity in mAh\n";
  metrics += "# TYPE ups_bms_capacity_remaining gauge\n";
  metrics += "ups_bms_capacity_remaining " + String(state.bms.capacity_remaining) + "\n\n";
  
  metrics += "# HELP ups_bms_connected BMS connection status (0=disconnected, 1=connected)\n";
  metrics += "# TYPE ups_bms_connected gauge\n";
  metrics += "ups_bms_connected " + String(state.bms.is_connected ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_bms_balancing_active Cell balancing active status (0=inactive, 1=active)\n";
  metrics += "# TYPE ups_bms_balancing_active gauge\n";
  metrics += "ups_bms_balancing_active " + String(state.bms.balancing_active ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_bms_fault_type BMS fault type (0=none, see BMS_Fault_t enum)\n";
  metrics += "# TYPE ups_bms_fault_type gauge\n";
  metrics += "ups_bms_fault_type " + String(state.bms.fault_type) + "\n\n";
  
  // Cell voltages
  metrics += "# HELP ups_bms_cell_voltage Individual cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage gauge\n";
  for (int i = 0; i < 5; i++) {
    metrics += "ups_bms_cell_voltage{cell=\"" + String(i + 1) + "\"} " + String(state.bms.cell_voltages[i]) + "\n";
  }
  metrics += "\n";
  
  metrics += "# HELP ups_bms_cell_voltage_min Minimum cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage_min gauge\n";
  metrics += "ups_bms_cell_voltage_min " + String(state.bms.cell_voltage_min) + "\n\n";
  
  metrics += "# HELP ups_bms_cell_voltage_max Maximum cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage_max gauge\n";
  metrics += "ups_bms_cell_voltage_max " + String(state.bms.cell_voltage_max) + "\n\n";
  
  metrics += "# HELP ups_bms_cell_voltage_avg Average cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage_avg gauge\n";
  metrics += "ups_bms_cell_voltage_avg " + String(state.bms.cell_voltage_avg) + "\n\n";

  // Internal resistance
  metrics += "# HELP ups_bms_cell_ir Estimated cell internal resistance in mΩ\n";
  metrics += "# TYPE ups_bms_cell_ir gauge\n";
  for (int i = 0; i < 5; i++) {
    metrics += "ups_bms_cell_ir{cell=\"" + String(i + 1) + "\"} " + String(state.bms.cell_internal_resistance[i], 1) + "\n";
  }
  metrics += "\n";

  metrics += "# HELP ups_bms_ir_sample_count Number of internal resistance measurements\n";
  metrics += "# TYPE ups_bms_ir_sample_count gauge\n";
  metrics += "ups_bms_ir_sample_count " + String(state.bms.ir_sample_count) + "\n\n";
  
  // Power metrics
  metrics += "# HELP ups_power_input_voltage Input voltage in millivolts\n";
  metrics += "# TYPE ups_power_input_voltage gauge\n";
  metrics += "ups_power_input_voltage " + String(state.power.input_voltage) + "\n\n";
  
  metrics += "# HELP ups_power_input_current Input current in milliamperes\n";
  metrics += "# TYPE ups_power_input_current gauge\n";
  metrics += "ups_power_input_current " + String(state.power.input_current) + "\n\n";
  
  metrics += "# HELP ups_power_output_power Output power in milliwatts\n";
  metrics += "# TYPE ups_power_output_power gauge\n";
  metrics += "ups_power_output_power " + String(state.power.output_power) + "\n\n";
  
  metrics += "# HELP ups_power_battery_voltage Battery voltage in millivolts\n";
  metrics += "# TYPE ups_power_battery_voltage gauge\n";
  metrics += "ups_power_battery_voltage " + String(state.power.battery_voltage) + "\n\n";
  
  metrics += "# HELP ups_power_battery_current Battery current in milliamperes\n";
  metrics += "# TYPE ups_power_battery_current gauge\n";
  metrics += "ups_power_battery_current " + String(state.power.battery_current) + "\n\n";
  
  metrics += "# HELP ups_power_ac_present AC power present status (0=absent, 1=present)\n";
  metrics += "# TYPE ups_power_ac_present gauge\n";
  metrics += "ups_power_ac_present " + String(state.power.ac_present ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_charger_enabled Charger enabled status (0=disabled, 1=enabled)\n";
  metrics += "# TYPE ups_power_charger_enabled gauge\n";
  metrics += "ups_power_charger_enabled " + String(state.power.charger_enabled ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_hybrid_mode Hybrid mode status (0=disabled, 1=enabled)\n";
  metrics += "# TYPE ups_power_hybrid_mode gauge\n";
  metrics += "ups_power_hybrid_mode " + String(state.power.hybrid_mode ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_fault_type Power fault type (0=none, see Power_Fault_Type_t enum)\n";
  metrics += "# TYPE ups_power_fault_type gauge\n";
  metrics += "ups_power_fault_type " + String(state.power.fault_type) + "\n\n";
  
  metrics += "# HELP ups_power_bq24780s_connected BQ24780S/BQ24800 chip connection status (0=disconnected, 1=connected)\n";
  metrics += "# TYPE ups_power_bq24780s_connected gauge\n";
  metrics += "ups_power_bq24780s_connected " + String(state.power.bq24780s_connected ? 1 : 0) + "\n\n";

  metrics += "# HELP ups_power_chip_variant Charger chip variant (0=BQ24780S, 1=BQ24800)\n";
  metrics += "# TYPE ups_power_chip_variant gauge\n";
  metrics += "ups_power_chip_variant " + String(state.power.chip_variant) + "\n\n";
  
  metrics += "# HELP ups_power_prochot_status PROCHOT pin status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_power_prochot_status gauge\n";
  metrics += "ups_power_prochot_status " + String(state.power.prochot_status ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_tbstat_status TB_STAT pin status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_power_tbstat_status gauge\n";
  metrics += "ups_power_tbstat_status " + String(state.power.tbstat_status ? 1 : 0) + "\n\n";
  
  // Protection status
  metrics += "# HELP ups_protection_over_current Over-current protection status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_protection_over_current gauge\n";
  metrics += "ups_protection_over_current " + String(state.over_current_protection ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_protection_over_temp Over-temperature protection status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_protection_over_temp gauge\n";
  metrics += "ups_protection_over_temp " + String(state.over_temp_protection ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_protection_short_circuit Short-circuit protection status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_protection_short_circuit gauge\n";
  metrics += "ups_protection_short_circuit " + String(state.short_circuit_protection ? 1 : 0) + "\n\n";

  // Self-consumption metrics
  metrics += "# HELP ups_self_consumption_mA System self-consumption current in mA (0=not calculated)\n";
  metrics += "# TYPE ups_self_consumption_mA gauge\n";
  metrics += "ups_self_consumption_mA " + String(state.self_consumption_mA, 2) + "\n\n";

  metrics += "# HELP ups_sc_segment_count Number of valid quiescent segments used\n";
  metrics += "# TYPE ups_sc_segment_count gauge\n";
  metrics += "ups_sc_segment_count " + String(state.sc_segment_count) + "\n\n";

  metrics += "# HELP ups_sc_total_segments Total quiescent segments found (including invalid)\n";
  metrics += "# TYPE ups_sc_total_segments gauge\n";
  metrics += "ups_sc_total_segments " + String(state.sc_total_segments) + "\n\n";

  metrics += "# HELP ups_sc_last_check Last self-consumption analysis check time (Unix timestamp)\n";
  metrics += "# TYPE ups_sc_last_check gauge\n";
  metrics += "ups_sc_last_check " + String(state.sc_last_check) + "\n\n";

  request->send(200, "text/plain; version=0.0.4; charset=utf-8", metrics);
}

// =============================================================================
// Authentication - 登录页 + Session Token (Cookie)
// =============================================================================

// 从 Cookie 头中提取 UPSSESSION token，成功返回 true 并填充 out（33 字节）
static bool extractSessionToken(AsyncWebServerRequest* request, char* out) {
  if (!request->hasHeader("Cookie")) return false;
  const String& cookies = request->header("Cookie");
  int pos = cookies.indexOf("UPSSESSION=");
  if (pos < 0) return false;
  pos += 11; // strlen("UPSSESSION=")
  int end = cookies.indexOf(';', pos);
  if (end < 0) end = cookies.length();
  int len = end - pos;
  if (len != 32) return false;
  memcpy(out, cookies.c_str() + pos, 32);
  out[32] = '\0';
  return true;
}

// 生成 32 字符 hex token（128bit 硬件随机数）
static void generateSessionToken(char* out) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i += 8) {
    uint32_t r = esp_random();
    for (int j = 0; j < 8; j++) {
      out[i + j] = hex[r & 0xF];
      r >>= 4;
    }
  }
  out[32] = '\0';
}

const char* WebServer::createSession() {
  // 找空位；无空位时淘汰最久未活动的会话
  unsigned long now = millis();
  int slot = -1;
  unsigned long oldest = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions_[i].valid) { slot = i; break; }
    unsigned long idle = now - sessions_[i].last_seen;
    if (idle >= oldest) { oldest = idle; slot = i; }
  }
  generateSessionToken(sessions_[slot].token);
  sessions_[slot].last_seen = now;
  sessions_[slot].valid = true;
  return sessions_[slot].token;
}

bool WebServer::checkSessionCookie(AsyncWebServerRequest* request) {
  char token[33];
  if (!extractSessionToken(request, token)) return false;

  unsigned long now = millis();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions_[i].valid) continue;
    // 过期清理（滑动 TTL）
    if (now - sessions_[i].last_seen > SESSION_TTL_MS) {
      sessions_[i].valid = false;
      continue;
    }
    if (strcmp(sessions_[i].token, token) == 0) {
      sessions_[i].last_seen = now; // 活动续期
      return true;
    }
  }
  return false;
}

void WebServer::destroySessionFromRequest(AsyncWebServerRequest* request) {
  char token[33];
  if (!extractSessionToken(request, token)) return;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions_[i].valid && strcmp(sessions_[i].token, token) == 0) {
      sessions_[i].valid = false;
      return;
    }
  }
}

void WebServer::invalidateAllSessions() {
  for (int i = 0; i < MAX_SESSIONS; i++) sessions_[i].valid = false;
}

// 判定当前请求是否已认证（不发送任何响应）
bool WebServer::isAuthenticated(AsyncWebServerRequest* request) {
  if (!configManager->hasWebCredentials()) {
    // 凭证未设置：配置模式放行（初始向导需可访问），正常模式拒绝
    return systemManager == nullptr;
  }
  // 优先 Cookie 会话；其次 HTTP Basic Auth（方便 curl/HomeAssistant 等脚本调用）
  if (checkSessionCookie(request)) return true;
  if (request->authenticate(configManager->getWebUsername(), configManager->getWebPassword())) return true;
  return false;
}

// 认证门卫：未认证时发送 401 JSON（API 场景），返回 false
bool WebServer::ensureAuthenticated(AsyncWebServerRequest* request) {
  if (isAuthenticated(request)) return true;

  if (configManager->hasWebCredentials()) {
    request->send(401, "application/json",
      "{\"success\":false,\"error\":\"unauthorized\","
      "\"message\":\"Login required. Visit / to log in.\"}");
  } else {
    // 正常运行模式但凭证为空（如旧固件升级）：要求先设置账户
    request->send(403, "application/json",
      "{\"success\":false,\"error\":\"credentials_not_set\","
      "\"message\":\"Access account not configured. Visit / to set username and password.\"}");
  }
  return false;
}

bool WebServer::parseCredentials(AsyncWebServerRequest* request, String& username, String& password) {
  String body;
  if (!takeBody(request, body)) {
    sendErrorResponse(request, "Missing request body", 400);
    return false;
  }

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body)) {
    sendErrorResponse(request, "Invalid JSON format", 400);
    return false;
  }

  const char* u = doc["username"];
  const char* p = doc["password"];
  if (!u || !p) {
    sendErrorResponse(request, "Missing username or password", 400);
    return false;
  }

  username = u;
  password = p;
  return true;
}

void WebServer::sendJsonWithNewSession(AsyncWebServerRequest* request, const char* json) {
  const char* token = createSession();
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
  char cookie[96];
  snprintf(cookie, sizeof(cookie), "UPSSESSION=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400", token);
  response->addHeader("Set-Cookie", cookie);
  request->send(response);
}

void WebServer::handleAuthLogin(AsyncWebServerRequest* request) {
  // 暴力破解限制：锁定期内直接拒绝
  unsigned long now = millis();
  if (login_lockout_until_ != 0 && (long)(login_lockout_until_ - now) > 0) {
    request->send(429, "application/json",
      "{\"success\":false,\"message\":\"Too many failed attempts, try again later\"}");
    discardBody(request);
    return;
  }

  if (!configManager->hasWebCredentials()) {
    sendErrorResponse(request, "Credentials not configured", 403);
    discardBody(request);
    return;
  }

  String username;
  String password;
  if (!parseCredentials(request, username, password)) return;

  if (strcmp(username.c_str(), configManager->getWebUsername()) != 0 ||
      strcmp(password.c_str(), configManager->getWebPassword()) != 0) {
    login_failures_++;
    if (login_failures_ >= 5) {
      login_lockout_until_ = now + 60000UL; // 锁定 60s
      login_failures_ = 0;
      DBG.println(F("[WebServer] Login locked out for 60s (too many failures)"));
    }
    request->send(401, "application/json",
      "{\"success\":false,\"message\":\"Invalid username or password\"}");
    return;
  }

  // 登录成功
  login_failures_ = 0;
  login_lockout_until_ = 0;
  sendJsonWithNewSession(request, "{\"success\":true,\"message\":\"Logged in\"}");
  DBG.println(F("[WebServer] Login OK, session created"));
}

void WebServer::handleAuthLogout(AsyncWebServerRequest* request) {
  destroySessionFromRequest(request);
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json",
    "{\"success\":true,\"message\":\"Logged out\"}");
  response->addHeader("Set-Cookie", "UPSSESSION=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  request->send(response);
}

void WebServer::handleAuthSetup(AsyncWebServerRequest* request) {
  // 仅允许在凭证未设置时调用（首次设置无需认证；已设置后必须走 /api/auth/change）
  if (configManager->hasWebCredentials()) {
    sendErrorResponse(request, "Credentials already configured, use /api/auth/change", 403);
    discardBody(request);
    return;
  }

  String username;
  String password;
  if (!parseCredentials(request, username, password)) return;

  if (!configManager->setWebCredentials(username.c_str(), password.c_str())) {
    sendErrorResponse(request, "Invalid credentials: username 1-32 chars, password 8-64 chars", 400);
    return;
  }

  DBG.println(F("[WebServer] Web access credentials configured"));

  // 首次设置成功后直接建立会话，免去二次登录
  sendJsonWithNewSession(request, "{\"success\":true,\"message\":\"Credentials saved\"}");
}

void WebServer::handleAuthChange(AsyncWebServerRequest* request) {
  // 修改凭证：认证已由 onAuthBody() 统一处理
  String username;
  String password;
  if (!parseCredentials(request, username, password)) return;

  if (!configManager->setWebCredentials(username.c_str(), password.c_str())) {
    sendErrorResponse(request, "Invalid credentials: username 1-32 chars, password 8-64 chars", 400);
    return;
  }

  // 凭证变更后吊销全部会话，所有客户端（包括本机）必须用新凭证重新登录
  invalidateAllSessions();

  DBG.println(F("[WebServer] Web access credentials changed, all sessions invalidated"));
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json",
    "{\"success\":true,\"message\":\"Credentials updated, please re-login\"}");
  response->addHeader("Set-Cookie", "UPSSESSION=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  request->send(response);
}

// =============================================================================
// Configuration Page Handlers
// =============================================================================

void WebServer::handleRoot(AsyncWebServerRequest* request) {
  if (configManager->hasWebCredentials()) {
    // 已设置凭证：未登录则显示登录页面
    if (!isAuthenticated(request)) {
      request->send_P(200, "text/html", AUTH_LOGIN_PAGE);
      return;
    }
  } else if (systemManager != nullptr) {
    // 正常模式但凭证为空（旧固件升级场景）：强制先设置账户密码
    request->send_P(200, "text/html", AUTH_SETUP_PAGE);
    return;
  }
  // 已登录，或配置模式且凭证为空（向导内强制设置凭证）
  renderSPA(request);
}

static String buildChargingWindowsJson(const void* windows, uint8_t count) {
  // 使用匿名结构避免依赖 power_management.h
  struct Window { uint8_t day_mask; uint8_t start_hour; uint8_t end_hour; };
  const Window* w = (const Window*)windows;

  String json = "[";
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(i);
    json += ",\"day_mask\":" + String(w[i].day_mask);
    json += ",\"start_hour\":" + String(w[i].start_hour);
    json += ",\"end_hour\":" + String(w[i].end_hour) + "}";
  }
  json += "]";
  return json;
}

void WebServer::renderSPA(AsyncWebServerRequest* request) {
  const Configuration* sysConfig = configManager->getSystemConfig();
  const BMS_Config_t* bmsConfig = configManager->getBMSConfig();
  const Power_Config_t* powerConfig = configManager->getPowerConfig();

  // 配置模式检测
  bool isConfigMode = (systemManager == nullptr);

  // 获取系统状态（仅正常模式使用）
  const System_Global_State* statePtr = nullptr;
  if (!isConfigMode) {
    statePtr = &systemManager->getGlobalState();
  }

  // 计算总缓冲区大小：HTML模板 + CSS + JavaScript + 大量额外空间用于替换和拼接
  size_t htmlSize = strlen_P(SPA_PAGE_TEMPLATE);
  size_t cssSize = strlen_P(COMMON_CSS) + strlen_P(CONFIG_CSS) + strlen_P(OTA_CSS) + strlen_P(WIZARD_CSS);
  size_t jsSize = strlen_P(SPA_PAGE_JS);
  // 增加安全余量：原始大小的 3 倍，确保 REPLACE 操作有足够空间
  size_t totalSize = (htmlSize + cssSize + jsSize) * 3;
  
  char* buffer = new char[totalSize];
  char* tempBuffer = new char[totalSize];
  
  // 第一步：复制 HTML 模板
  strcpy_P(buffer, SPA_PAGE_TEMPLATE);
  
  // 第二步：替换 CSS 占位符
  // 找到 <style id="dynamic-css"></style> 并替换内容
  const char* cssPlaceholderStart = strstr_P(buffer, PSTR("<style id=\"dynamic-css\"></style>"));

  if (cssPlaceholderStart) {
    // 找到占位符位置
    size_t placeholderPos = cssPlaceholderStart - buffer;
    const char* afterCss = cssPlaceholderStart + strlen("<style id=\"dynamic-css\"></style>");
    size_t afterCssLen = strlen(afterCss);
    
    // 创建临时缓冲区保存后半部分
    char* afterPart = new char[afterCssLen + 1];
    strcpy(afterPart, afterCss);
    
    // 在占位符处截断
    buffer[placeholderPos] = '\0';
    
    // 拼接：前半部分 + <style> + CSS + </style> + 后半部分
    strcat(buffer, "<style>");
    strcat_P(buffer, COMMON_CSS);
    strcat_P(buffer, CONFIG_CSS);
    strcat_P(buffer, OTA_CSS);
    strcat_P(buffer, WIZARD_CSS);
    strcat(buffer, "</style>");
    strcat(buffer, afterPart);
    
    delete[] afterPart;
  }

  // 第三步：替换 JavaScript 占位符
  const char* jsPlaceholderStart = strstr_P(buffer, PSTR("<script id=\"dynamic-js\"></script>"));
  
  if (jsPlaceholderStart) {
    // 找到占位符位置
    size_t placeholderPos = jsPlaceholderStart - buffer;
    const char* afterJs = jsPlaceholderStart + strlen("<script id=\"dynamic-js\"></script>");
    size_t afterJsLen = strlen(afterJs);
    
    // 创建临时缓冲区保存后半部分
    char* afterPart = new char[afterJsLen + 1];
    strcpy(afterPart, afterJs);
    
    // 在占位符处截断
    buffer[placeholderPos] = '\0';
    
    // 拼接：前半部分 + <script> + JS + </script> + 后半部分
    strcat(buffer, "<script>");
    strcat_P(buffer, SPA_PAGE_JS);
    strcat(buffer, "</script>");
    strcat(buffer, afterPart);
    
    delete[] afterPart;
  }
  
  strcpy(tempBuffer, buffer);
  
  #define REPLACE(key, val) do { replaceStringInBuffer(tempBuffer, totalSize, key, val, buffer); strcpy(tempBuffer, buffer); } while(0)
  #define REPLACE_FMT(key, fmt, val) do { char tmp[32]; snprintf(tmp, sizeof(tmp), fmt, val); REPLACE(key, tmp); } while(0)
  #define REPLACE_CHK(cond) REPLACE("%" #cond "_CHECKED%", (cond) ? "checked" : "")
  
  // System config
  
  // System config
  REPLACE("%WIFI_SSID%", sysConfig->wifi_ssid);
  REPLACE("%WIFI_PASS%", sysConfig->wifi_pass[0] ? sysConfig->wifi_pass : "");
  REPLACE("%BUZZER_STATUS%", sysConfig->buzzer_enabled ? I18n::get(STR_ENABLED) : I18n::get(STR_DISABLED));
  REPLACE("%BUZZER_CHECKED%", sysConfig->buzzer_enabled ? "checked" : "");
  REPLACE_FMT("%VOLUME_VALUE%", "%d", sysConfig->buzzer_volume);
  REPLACE_FMT("%VOLUME_LEVEL%", "%d%%", sysConfig->buzzer_volume);
  REPLACE_FMT("%LIGHT_VALUE%", "%d", sysConfig->led_brightness);
  REPLACE_FMT("%LIGHT_BRIGHTNESS%", "%d%%", sysConfig->led_brightness);
  
  // HID 配置
  REPLACE("%HID_CHECKED%", sysConfig->hid_enabled ? "checked" : "");
  REPLACE("%HID_MODE_MAH%", sysConfig->hid_report_mode == 0 ? " selected" : "");
  REPLACE("%HID_MODE_MWH%", sysConfig->hid_report_mode == 1 ? " selected" : "");
  REPLACE("%HID_MODE_PCT%", sysConfig->hid_report_mode == 2 ? " selected" : "");

  // MQTT 配置
  REPLACE("%MQTT_CHECKED%", (sysConfig->mqtt_broker[0] != '\0' && sysConfig->mqtt_port > 0) ? "checked" : "");
  REPLACE("%MQTT_BROKER%", sysConfig->mqtt_broker);
  REPLACE_FMT("%MQTT_PORT%", "%d", sysConfig->mqtt_port);
  REPLACE("%MQTT_USERNAME%", sysConfig->mqtt_username);
  REPLACE("%MQTT_PASSWORD%", sysConfig->mqtt_password);

  // 小米传感器桥接配置
  REPLACE("%XIAOMI_CHECKED%", sysConfig->xiaomi_sensor_enabled ? "checked" : "");
  REPLACE("%XIAOMI_SECTION_DISPLAY%", g_is_new_board ? "block" : "none");

  // IP 模式配置 - 新增
  REPLACE("%IP_MODE_DHCP%", sysConfig->use_static_ip ? "" : " selected");
  REPLACE("%IP_MODE_STATIC%", sysConfig->use_static_ip ? " selected" : "");
  REPLACE("%STATIC_IP_DISPLAY%", sysConfig->use_static_ip ? "block" : "none");
   REPLACE("%STATIC_IP%", sysConfig->static_ip);
   REPLACE("%STATIC_GATEWAY%", sysConfig->static_gateway);
   REPLACE("%STATIC_SUBNET%", sysConfig->static_subnet);
   REPLACE("%STATIC_DNS%", sysConfig->static_dns);
   REPLACE("%NTP_SERVER%", sysConfig->ntp_server);

   // BMS config
  REPLACE_FMT("%CELL_COUNT%", "%d", bmsConfig->cell_count);
  REPLACE_FMT("%CAPACITY%", "%d", bmsConfig->nominal_capacity_mAh);
  REPLACE_FMT("%BMS_CHARGE_CURRENT%", "%d", bmsConfig->max_charge_current);
  REPLACE_FMT("%PWR_CHARGE_CURRENT%", "%d", powerConfig->max_charge_current);
  REPLACE_FMT("%PWR_DISCHARGE_CURRENT%", "%d", powerConfig->max_discharge_current);
  
  REPLACE("%BMS_CELL_COUNT_3%", (bmsConfig->cell_count == 3) ? " selected" : "");
  REPLACE("%BMS_CELL_COUNT_4%", (bmsConfig->cell_count == 4) ? " selected" : "");
  REPLACE("%BMS_CELL_COUNT_5%", (bmsConfig->cell_count == 5) ? " selected" : "");
  
  REPLACE_FMT("%BMS_NOMINAL_CAPACITY%", "%d", bmsConfig->nominal_capacity_mAh);
  REPLACE_FMT("%BMS_CELL_OV%", "%d", bmsConfig->cell_ov_threshold);
  REPLACE_FMT("%BMS_CELL_UV%", "%d", bmsConfig->cell_uv_threshold);
  REPLACE_FMT("%BMS_CELL_OV_RECOVER%", "%d", bmsConfig->cell_ov_recover);
  REPLACE_FMT("%BMS_CELL_UV_RECOVER%", "%d", bmsConfig->cell_uv_recover);
  REPLACE_FMT("%BMS_MAX_CHARGE%", "%d", bmsConfig->max_charge_current);
  REPLACE_FMT("%BMS_MAX_DISCHARGE%", "%d", bmsConfig->max_discharge_current);
  REPLACE_FMT("%BMS_SHORT_CIRCUIT%", "%d", bmsConfig->short_circuit_threshold);
  REPLACE_FMT("%BMS_OVERHEAT_THRESHOLD%", "%.1f", bmsConfig->temp_overheat_threshold);
  

  // 正常运行模式：完整替换所有配置
  REPLACE("%BMS_BALANCING_CHECKED%", bmsConfig->balancing_enabled ? "checked" : "");
  REPLACE_FMT("%BMS_BALANCING_DIFF%", "%.1f", bmsConfig->balancing_voltage_diff);
  REPLACE_FMT("%POWER_MAX_CHARGE%", "%d", powerConfig->max_charge_current);
  REPLACE_FMT("%POWER_CHARGE_VOLTAGE%", "%d", powerConfig->charge_voltage_limit);
  REPLACE_FMT("%POWER_CHARGE_SOC_START%", "%.0f", powerConfig->charge_soc_start);
  REPLACE_FMT("%POWER_CHARGE_SOC_STOP%", "%.0f", powerConfig->charge_soc_stop);
  REPLACE_FMT("%POWER_MAX_DISCHARGE%", "%d", powerConfig->max_discharge_current);
  REPLACE_FMT("%POWER_DISCHARGE_SOC_STOP%", "%.0f", powerConfig->discharge_soc_stop);
  REPLACE("%POWER_HYBRID_CHECKED%", powerConfig->enable_hybrid_boost ? "checked" : "");
  REPLACE_FMT("%POWER_VSYS_MIN%", "%d", powerConfig->vsys_min_mV);
  REPLACE_FMT("%POWER_OVER_CURRENT%", "%d", powerConfig->over_current_threshold);
  REPLACE_FMT("%POWER_OVER_TEMP%", "%.1f", powerConfig->over_temp_threshold);
  REPLACE_FMT("%POWER_CHARGE_TEMP_HIGH%", "%.1f", powerConfig->charge_temp_high_limit);
  REPLACE_FMT("%POWER_CHARGE_TEMP_LOW%", "%.1f", powerConfig->charge_temp_low_limit);

  if (!isConfigMode && statePtr != nullptr) {
    String firmwareVersion = String(statePtr->system.firmware_version);
    char versionBuf[32];
    snprintf(versionBuf, sizeof(versionBuf), "%s", firmwareVersion.c_str());
    REPLACE("%FIRMWARE_VERSION%", versionBuf);
  } else {
    REPLACE("%FIRMWARE_VERSION%", "Config Mode");
  }

  char spaceBuf[32];
  snprintf(spaceBuf, sizeof(spaceBuf), "%lu", (unsigned long)(ESP.getFreeSketchSpace() / 1024));
  REPLACE("%FREE_SKETCH_SPACE%", spaceBuf);
  char flashBuf[32];
  snprintf(flashBuf, sizeof(flashBuf), "%lu", (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));
  REPLACE("%FLASH_SIZE%", flashBuf);


  // 将时间窗口数据注入到 HTML 页面
  String windowsJson = isConfigMode ? "[]" : buildChargingWindowsJson(powerConfig->charging_windows, powerConfig->charging_window_count);
  char windowInitCode[2048];
  snprintf(windowInitCode, sizeof(windowInitCode),
           "<script>window.IW=%s;</script>",
           windowsJson.c_str());

  #undef REPLACE
  #undef REPLACE_FMT
  #undef REPLACE_CHK

  // 注入配置模式标记和当前语言到 </head> 后（确保在 JS 执行前定义）
  char configModeScript[128];
  snprintf(configModeScript, sizeof(configModeScript),
    "<script>window.CONFIG_MODE=%d;window.CURLANG='%s';</script>",
    isConfigMode ? 1 : 0, I18n::getLangCode());
  char* headEndPos = strstr(tempBuffer, "</head>");
  if (headEndPos) {
    size_t headPos = headEndPos - tempBuffer + strlen("</head>");
    size_t afterHeadLen = strlen(headEndPos);
    char* afterHead = new char[afterHeadLen + strlen(configModeScript) + 1];
    strcpy(afterHead, headEndPos);

    headEndPos[0] = '\0';
    strcat(tempBuffer, configModeScript);
    strcat(tempBuffer, afterHead);
    delete[] afterHead;
  }

  // 注入初始化脚本到 </body> 前
  char* bodyEndPos = strstr(tempBuffer, "</body>");
  if (bodyEndPos) {
    size_t bodyPos = bodyEndPos - tempBuffer;
    size_t afterBodyLen = strlen(bodyEndPos);
    char* afterBody = new char[afterBodyLen + strlen(windowInitCode) + 1];
    strcpy(afterBody, bodyEndPos);

    tempBuffer[bodyPos] = '\0';
    strcat(tempBuffer, windowInitCode);
    strcat(tempBuffer, afterBody);
    delete[] afterBody;
  }

  request->send(200, "text/html", tempBuffer);
  delete[] buffer;
  delete[] tempBuffer;
}

// =============================================================================
// Save Configuration Handler
// =============================================================================

void WebServer::handleSaveConfig(AsyncWebServerRequest* request) {
  DBG.println(F("WebServer: Handling save config request"));
  
  String jsonString;
  if (!takeBody(request, jsonString)) {
    if (request->hasParam("plain", true)) {
      jsonString = request->getParam("plain", true)->value();
    } else if (request->hasArg("plain")) {
      jsonString = request->arg("plain");
    } else {
      sendErrorResponse(request, "Missing request body", 400);
      return;
    }
  }
  
  // 2048 → 3072：请求体新增 auth 字段（配置向导首次保存携带账户密码）
  StaticJsonDocument<3072> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    DBG.printf_P(PSTR("JSON parse error: %s\n"), error.c_str());
    sendErrorResponse(request, "Invalid JSON format", 400);
    return;
  }

  // 判断是否在配置模式（systemManager 为 nullptr 表示配置模式）
  bool isConfigMode = (systemManager == nullptr);

  // 配置模式下必须设置访问账户密码（凭证已存在则跳过）
  // 先做格式预检，实际写入放在配置校验通过之后，避免配置失败但凭证已生效
  const char* authUser = nullptr;
  const char* authPass = nullptr;
  if (isConfigMode && !configManager->hasWebCredentials()) {
    authUser = doc["auth"]["username"];
    authPass = doc["auth"]["password"];
    if (!authUser || !authPass) {
      sendErrorResponse(request, "Access account required: provide auth.username and auth.password", 400);
      return;
    }
    size_t ulen = strlen(authUser);
    size_t plen = strlen(authPass);
    if (ulen < 1 || ulen > 32 || plen < 8 || plen > 64) {
      sendErrorResponse(request, "Invalid access account: username 1-32 chars, password 8-64 chars", 400);
      return;
    }
  }

  // 字段级校验错误收集
  DynamicJsonDocument errorDoc(2048);
  if (!updateConfigurationFromRequest(doc, errorDoc)) {
    errorDoc["success"] = false;
    if (!errorDoc.containsKey("message")) {
      errorDoc["message"] = "Configuration validation failed";
    }
    String response;
    serializeJson(errorDoc, response);
    request->send(400, "application/json", response);
    return;
  }

  // 配置校验通过，保存访问凭证（如有）
  if (authUser && authPass) {
    if (!configManager->setWebCredentials(authUser, authPass)) {
      sendErrorResponse(request, "Failed to save access account", 500);
      return;
    }
    DBG.println(F("[WebServer] Web access credentials set via config wizard"));
  }

  if (!configManager->saveConfiguration()) {
    sendErrorResponse(request, "Failed to save configuration to flash", 500);
    return;
  }

  DBG.println(F("Configuration saved successfully"));
  
  StaticJsonDocument<256> responseDoc;
  responseDoc["success"] = true;
  
  if (isConfigMode) {
    // 配置模式：需要重启才能应用配置
    responseDoc["message"] = "Configuration saved successfully. Device will reboot...";
    responseDoc["restart_required"] = true;

    char responseBuffer[512];
    serializeJson(responseDoc, responseBuffer);
    request->send(200, "application/json", responseBuffer);

    // 确保响应完全发送后再重启
    delay(100);

    DBG.println(F("==========================================="));
    DBG.println(F("Configuration saved. Rebooting..."));
    DBG.println(F("==========================================="));

    ESP.restart();
  } else {
    // 正常运行模式：热更新，不需要重启
    // 事件已经通过 updateSystemConfig/updateBMSConfig/updatePowerConfig 发布
    // SystemManagement 和 WiFiManager 会通过事件监听自动应用配置
    responseDoc["message"] = "Configuration saved and applied successfully";
    responseDoc["restart_required"] = false;
    
    char responseBuffer[512];
    serializeJson(responseDoc, responseBuffer);
    request->send(200, "application/json", responseBuffer);
    
    DBG.println(F("Configuration applied via hot update (no restart required)"));
  }
  return;
}

// =============================================================================
// Helper Functions
// =============================================================================

void WebServer::sendErrorResponse(AsyncWebServerRequest* request, const String& message, int code) {
  DBG.printf_P(PSTR("Error response: %s (code: %d)\n"), message.c_str(), code);
  request->send(code, "application/json", "{\"success\":false,\"message\":\"" + message + "\"}");
}

void WebServer::sendConfigModeResponse(AsyncWebServerRequest* request) {
  StaticJsonDocument<256> doc;
  doc["status"] = "config_mode";
  doc["message"] = "Hardware modules not initialized";
  doc["timestamp"] = millis();
  
  char buffer[512];
  serializeJson(doc, buffer);
  request->send(200, "application/json", buffer);
}

// =============================================================================
// Configuration Update Logic
// =============================================================================

bool WebServer::updateConfigurationFromRequest(const JsonDocument& doc, JsonDocument& errorDoc) {
  DBG.println(F("Updating configuration from JSON"));

  // 字段级错误收集：非法字段显式报错并拒绝整个请求，不再 silent ignore
  JsonArray errors = errorDoc.createNestedArray("errors");
  auto addError = [&errors](const char* field, const char* message) {
    JsonObject e = errors.createNestedObject();
    e["field"] = field;
    e["message"] = message;
  };

  // 创建临时配置副本，避免直接修改内部配置导致变化检测失败
  Configuration tempSysConfig = *configManager->getSystemConfig();
  BMS_Config_t tempBmsConfig = *configManager->getBMSConfig();
  Power_Config_t tempPowerConfig = *configManager->getPowerConfig();

  // Update System Configuration
  if (doc.containsKey("system")) {
    JsonVariantConst sys = doc["system"];

    if (sys.containsKey("wifi_ssid")) {
      const char* ssid = sys["wifi_ssid"];
      if (ssid) strlcpy(tempSysConfig.wifi_ssid, ssid, sizeof(tempSysConfig.wifi_ssid));
    }
    if (sys.containsKey("wifi_pass")) {
      const char* pass = sys["wifi_pass"];
      if (pass) strlcpy(tempSysConfig.wifi_pass, pass, sizeof(tempSysConfig.wifi_pass));
    }
    if (sys.containsKey("led_brightness")) {
      int val = sys["led_brightness"];
      if (val >= 0 && val <= 100) tempSysConfig.led_brightness = (uint8_t)val;
      else addError("system.led_brightness", "must be 0-100");
    }
    if (sys.containsKey("buzzer_enabled")) {
      tempSysConfig.buzzer_enabled = sys["buzzer_enabled"];
    }
    if (sys.containsKey("volume_level")) {
      int val = sys["volume_level"];
      if (val >= 0 && val <= 100) tempSysConfig.buzzer_volume = (uint8_t)val;
      else addError("system.volume_level", "must be 0-100");
    }

    // ========== 处理 HID 配置 ==========
    if (sys.containsKey("hid_enabled")) {
      tempSysConfig.hid_enabled = sys["hid_enabled"];
    }
    if (sys.containsKey("hid_report_mode")) {
      int val = sys["hid_report_mode"];
      if (val >= 0 && val <= 2) tempSysConfig.hid_report_mode = (uint8_t)val;
      else addError("system.hid_report_mode", "must be 0-2");
    }
    // ================================

    // ========== 处理 MQTT 配置 ==========
    if (sys.containsKey("mqtt_enabled")) {
      bool mqtt_en = sys["mqtt_enabled"];
      if (mqtt_en) {
        // 启用 MQTT，需要填写 broker 和 port
        if (sys.containsKey("mqtt_broker") && strlen(sys["mqtt_broker"].as<const char*>()) > 0) {
          const char* broker = sys["mqtt_broker"];
          if (broker) strlcpy(tempSysConfig.mqtt_broker, broker, sizeof(tempSysConfig.mqtt_broker));
        }
        if (sys.containsKey("mqtt_port")) {
          uint32_t port = sys["mqtt_port"];
          if (port > 0 && port <= 65535) tempSysConfig.mqtt_port = (uint16_t)port;
          else addError("system.mqtt_port", "must be 1-65535");
        }
        if (sys.containsKey("mqtt_username") && strlen(sys["mqtt_username"].as<const char*>()) > 0) {
          const char* usr = sys["mqtt_username"];
          if (usr) strlcpy(tempSysConfig.mqtt_username, usr, sizeof(tempSysConfig.mqtt_username));
        }
        if (sys.containsKey("mqtt_password") && strlen(sys["mqtt_password"].as<const char*>()) > 0) {
          const char* pwd = sys["mqtt_password"];
          if (pwd) strlcpy(tempSysConfig.mqtt_password, pwd, sizeof(tempSysConfig.mqtt_password));
        }
      } else {
        // 禁用 MQTT，清空配置
        tempSysConfig.mqtt_broker[0] = '\0';
        tempSysConfig.mqtt_port = 0;
        tempSysConfig.mqtt_username[0] = '\0';
        tempSysConfig.mqtt_password[0] = '\0';
      }
    }
    // =================================

    // ========== 处理小米传感器桥接配置 ==========
    if (sys.containsKey("xiaomi_sensor_enabled")) {
      tempSysConfig.xiaomi_sensor_enabled = sys["xiaomi_sensor_enabled"];
    }
    // ===========================================

    // ========== 处理静态 IP 配置 - 新增 ==========
    if (sys.containsKey("use_static_ip")) {
      tempSysConfig.use_static_ip = sys["use_static_ip"];
    }
    if (sys.containsKey("static_ip") && strlen(sys["static_ip"].as<const char*>()) > 0) {
      const char* ip = sys["static_ip"];
      if (ip) strlcpy(tempSysConfig.static_ip, ip, sizeof(tempSysConfig.static_ip));
    }
    if (sys.containsKey("static_gateway") && strlen(sys["static_gateway"].as<const char*>()) > 0) {
      const char* gw = sys["static_gateway"];
      if (gw) strlcpy(tempSysConfig.static_gateway, gw, sizeof(tempSysConfig.static_gateway));
    }
    if (sys.containsKey("static_subnet") && strlen(sys["static_subnet"].as<const char*>()) > 0) {
      const char* sn = sys["static_subnet"];
      if (sn) strlcpy(tempSysConfig.static_subnet, sn, sizeof(tempSysConfig.static_subnet));
    }
    if (sys.containsKey("static_dns") && strlen(sys["static_dns"].as<const char*>()) > 0) {
      const char* dns = sys["static_dns"];
      if (dns) strlcpy(tempSysConfig.static_dns, dns, sizeof(tempSysConfig.static_dns));
    }
    if (sys.containsKey("ntp_server") && strlen(sys["ntp_server"].as<const char*>()) > 0) {
      const char* ntp = sys["ntp_server"];
      if (ntp) strlcpy(tempSysConfig.ntp_server, ntp, sizeof(tempSysConfig.ntp_server));
    }
    // ======================================
    
  }

  // Update BMS Configuration
  if (doc.containsKey("bms")) {
    JsonVariantConst bms = doc["bms"];

    if (bms.containsKey("cell_count")) {
      uint8_t val = bms["cell_count"];
      if (val >= 3 && val <= 5) tempBmsConfig.cell_count = val;
      else addError("bms.cell_count", "must be 3-5");
    }
    if (bms.containsKey("nominal_capacity_mAh")) {
      uint32_t val = bms["nominal_capacity_mAh"];
      if (val > 0 && val <= 50000) tempBmsConfig.nominal_capacity_mAh = val;
      else addError("bms.nominal_capacity_mAh", "must be 1-50000 mAh");
    }
    if (bms.containsKey("cell_ov_threshold")) {
      uint16_t val = bms["cell_ov_threshold"];
      if (val >= 4000 && val <= 4500) tempBmsConfig.cell_ov_threshold = val;
      else addError("bms.cell_ov_threshold", "must be 4000-4500 mV");
    }
    if (bms.containsKey("cell_uv_threshold")) {
      uint16_t val = bms["cell_uv_threshold"];
      if (val >= 2500 && val <= 3500) tempBmsConfig.cell_uv_threshold = val;
      else addError("bms.cell_uv_threshold", "must be 2500-3500 mV");
    }
    if (bms.containsKey("cell_ov_recover")) {
      uint16_t val = bms["cell_ov_recover"];
      if (val >= 4000 && val <= 4300) tempBmsConfig.cell_ov_recover = val;
      else addError("bms.cell_ov_recover", "must be 4000-4300 mV");
    }
    if (bms.containsKey("cell_uv_recover")) {
      uint16_t val = bms["cell_uv_recover"];
      if (val >= 2800 && val <= 3300) tempBmsConfig.cell_uv_recover = val;
      else addError("bms.cell_uv_recover", "must be 2800-3300 mV");
    }
    if (bms.containsKey("max_charge_current")) {
      uint16_t val = bms["max_charge_current"];
      if (val > 0 && val <= 10000) tempBmsConfig.max_charge_current = val;
      else addError("bms.max_charge_current", "must be 1-10000 mA");
    }
    if (bms.containsKey("max_discharge_current")) {
      uint16_t val = bms["max_discharge_current"];
      if (val > 0 && val <= 20000) tempBmsConfig.max_discharge_current = val;
      else addError("bms.max_discharge_current", "must be 1-20000 mA");
    }
    if (bms.containsKey("short_circuit_threshold")) {
      uint16_t val = bms["short_circuit_threshold"];
      if (val <= 30000) tempBmsConfig.short_circuit_threshold = val;
      else addError("bms.short_circuit_threshold", "must be <= 30000 mA");
    }
    if (bms.containsKey("temp_overheat_threshold")) {
      float val = bms["temp_overheat_threshold"];
      if (val >= 50.0f && val <= 80.0f) tempBmsConfig.temp_overheat_threshold = val;
      else addError("bms.temp_overheat_threshold", "must be 50-80 C");
    }
    if (bms.containsKey("balancing_enabled")) {
      tempBmsConfig.balancing_enabled = bms["balancing_enabled"];
    }
    if (bms.containsKey("balancing_voltage_diff")) {
      float val = bms["balancing_voltage_diff"];
      if (val >= 5.0f && val <= 100.0f) tempBmsConfig.balancing_voltage_diff = val;
      else addError("bms.balancing_voltage_diff", "must be 5-100 mV");
    }

  }

  // Update Power Configuration
  if (doc.containsKey("power")) {
    JsonVariantConst pwr = doc["power"];

    if (pwr.containsKey("max_charge_current")) {
      uint16_t val = pwr["max_charge_current"];
      if (val > 0 && val <= 10000) tempPowerConfig.max_charge_current = val;
      else addError("power.max_charge_current", "must be 1-10000 mA");
    }
    if (pwr.containsKey("charge_voltage_limit")) {
      uint16_t val = pwr["charge_voltage_limit"];
      if (val >= 10000 && val <= 18250) tempPowerConfig.charge_voltage_limit = val;
      else addError("power.charge_voltage_limit", "must be 10000-18250 mV");
    }
    if (pwr.containsKey("charge_soc_start")) {
      float val = pwr["charge_soc_start"];
      if (val >= 0.0f && val <= 90.0f) tempPowerConfig.charge_soc_start = val;
      else addError("power.charge_soc_start", "must be 0-90 %");
    }
    if (pwr.containsKey("charge_soc_stop")) {
      float val = pwr["charge_soc_stop"];
      if (val >= 50.0f && val <= 100.0f) tempPowerConfig.charge_soc_stop = val;
      else addError("power.charge_soc_stop", "must be 50-100 %");
    }
    if (pwr.containsKey("max_discharge_current")) {
      uint16_t val = pwr["max_discharge_current"];
      if (val > 0 && val <= 20000) tempPowerConfig.max_discharge_current = val;
      else addError("power.max_discharge_current", "must be 1-20000 mA");
    }
    if (pwr.containsKey("discharge_soc_stop")) {
      float val = pwr["discharge_soc_stop"];
      if (val >= 0.0f && val <= 30.0f) tempPowerConfig.discharge_soc_stop = val;
      else addError("power.discharge_soc_stop", "must be 0-30 %");
    }
    if (pwr.containsKey("enable_hybrid_boost")) {
      tempPowerConfig.enable_hybrid_boost = pwr["enable_hybrid_boost"];
    }
    if (pwr.containsKey("over_current_threshold")) {
      uint16_t val = pwr["over_current_threshold"];
      if (val <= 20000) tempPowerConfig.over_current_threshold = val;
      else addError("power.over_current_threshold", "must be <= 20000 mA");
    }
    if (pwr.containsKey("over_temp_threshold")) {
      float val = pwr["over_temp_threshold"];
      if (val >= 40.0f && val <= 100.0f) tempPowerConfig.over_temp_threshold = val;
      else addError("power.over_temp_threshold", "must be 40-100 C");
    }
    if (pwr.containsKey("charge_temp_high_limit")) {
      float val = pwr["charge_temp_high_limit"];
      if (val >= 30.0f && val <= 60.0f) tempPowerConfig.charge_temp_high_limit = val;
      else addError("power.charge_temp_high_limit", "must be 30-60 C");
    }
    if (pwr.containsKey("charge_temp_low_limit")) {
      float val = pwr["charge_temp_low_limit"];
      if (val >= -20.0f && val <= 10.0f) tempPowerConfig.charge_temp_low_limit = val;
      else addError("power.charge_temp_low_limit", "must be -20 to 10 C");
    }
    if (pwr.containsKey("vsys_min_mV")) {
      uint16_t val = pwr["vsys_min_mV"];
      if (val >= 5888 && val <= 16128) tempPowerConfig.vsys_min_mV = val; // 23*256 ~ 63*256
      else addError("power.vsys_min_mV", "must be 5888-16128 mV");
    }

    // ========== 处理时间窗口配置 ==========
    if (pwr.containsKey("charging_windows") && pwr.containsKey("charging_window_count")) {
      JsonArrayConst windowsArray = pwr["charging_windows"].as<JsonArrayConst>();
      uint8_t windowCount = pwr["charging_window_count"];
      
      DBG.printf_P(PSTR("[Config] Processing %d charging windows\n"), windowCount);
      
      // 首先清空所有时间窗口
      memset(tempPowerConfig.charging_windows, 0, sizeof(tempPowerConfig.charging_windows));
      tempPowerConfig.charging_window_count = 0;
      
      // 解析并保存有效的时间窗口（最多 5 个）
      uint8_t validWindowCount = 0;
      for (size_t i = 0; i < windowsArray.size() && validWindowCount < 5; i++) {
        JsonObjectConst window = windowsArray[i].as<JsonObjectConst>();
        
        if (window.containsKey("day_mask") &&
            window.containsKey("start_hour") &&
            window.containsKey("end_hour")) {
          
          uint8_t dayMask = window["day_mask"];
          uint8_t startHour = window["start_hour"];
          uint8_t endHour = window["end_hour"];
          
          // 验证数据有效性
          if (dayMask > 0 && dayMask <= 127 &&  // 至少有一天，且不超过 7 位
              startHour < 24 && endHour <= 24 &&  // 小时范围有效
              startHour < endHour) {  // 开始时间必须小于结束时间
            
            tempPowerConfig.charging_windows[validWindowCount].day_mask = dayMask;
            tempPowerConfig.charging_windows[validWindowCount].start_hour = startHour;
            tempPowerConfig.charging_windows[validWindowCount].end_hour = endHour;
            
            validWindowCount++;

            DBG.printf_P(PSTR("[Config] Window %d: mask=0x%02X, %02d:00-%02d:00\n"),
                           validWindowCount, dayMask, startHour, endHour);
          } else {
            DBG.printf_P(PSTR("[Config] Invalid window %d: mask=%d, %d:00-%d:00\n"),
                           i, dayMask, startHour, endHour);
            addError("power.charging_windows", "invalid window: day_mask 1-127, start_hour < end_hour <= 24");
          }
        }
      }
      
      // 更新实际窗口数量
      tempPowerConfig.charging_window_count = validWindowCount;
      DBG.printf_P(PSTR("[Config] Total valid charging windows: %d\n"), validWindowCount);
    }
    // ======================================
    
  }
  
  // ========== 字段级校验汇总 ==========
  // 任何字段非法则整体拒绝，不提交任何配置（两阶段：先全部校验，后统一提交）
  if (errors.size() > 0) {
    DBG.printf_P(PSTR("[Config] Validation failed with %d field error(s), nothing applied\n"),
                   (int)errors.size());
    return false;
  }

  // ========== 统一提交阶段 ==========
  bool success = true;
  if (doc.containsKey("system") && !configManager->updateSystemConfig(tempSysConfig, false)) {
    DBG.println(F("Error: Failed to update system config"));
    success = false;
  }
  if (doc.containsKey("bms") && !configManager->updateBMSConfig(tempBmsConfig, false)) {
    DBG.println(F("Error: Failed to update BMS config"));
    success = false;
  }
  if (doc.containsKey("power") && !configManager->updatePowerConfig(tempPowerConfig, false)) {
    DBG.println(F("Error: Failed to update power config"));
    success = false;
  }
  if (!success) {
    errorDoc["message"] = "Configuration rejected by ConfigManager validation";
    return false;
  }

  // ========== 处理语言配置 ==========
  if (doc.containsKey("lang")) {
    const char* lang = doc["lang"];
    if (lang && strcmp(lang, "en") == 0) {
      I18n::setLanguage(LANG_EN);
    } else {
      I18n::setLanguage(LANG_ZH);
    }
    DBG.printf_P(PSTR("[Config] Language set to: %s\n"), I18n::getLangCode());
  }
  // =================================

  DBG.println(F("All configs updated successfully"));
  return true;
}

void WebServer::replaceStringInBuffer(char* buffer, size_t bufferSize, const char* search, 
                                      const char* replace, char* tempBuffer) {
  if (!buffer || !search || !replace || !tempBuffer) return;
  
  size_t searchLen = strlen(search);
  if (searchLen == 0) return; // 防止空搜索字符串
  
  size_t replaceLen = strlen(replace);
  
  // 使用迭代而非递归，避免栈溢出
  while (true) {
    char* pos = strstr(buffer, search);
    if (!pos) break; // 没有更多匹配项，退出循环
    
    size_t prefixLen = pos - buffer;
    
    // 检查缓冲区是否足够
    size_t currentLen = strlen(buffer);
    size_t newLen = currentLen - searchLen + replaceLen;
    if (newLen >= bufferSize) {
      break; // 防止缓冲区溢出
    }
    
    // 复制前缀部分
    strncpy(tempBuffer, buffer, prefixLen);
    tempBuffer[prefixLen] = '\0';
    
    // 拼接替换内容
    strlcat(tempBuffer, replace, bufferSize);
    strlcat(tempBuffer, pos + searchLen, bufferSize);
    
    // 复制回原缓冲区
    strncpy(buffer, tempBuffer, bufferSize);
    buffer[bufferSize - 1] = '\0';
  }
}

// =============================================================================
// BMS Ship Mode Handler
// =============================================================================

void WebServer::handleBmsShipMode(AsyncWebServerRequest* request) {
  DBG.println(F("[WebServer] BMS Ship Mode request received"));
  
  // First, send response to frontend confirming receipt
  StaticJsonDocument<256> doc;
  doc["success"] = true;
  doc["message"] = "请求已接收，正在进入运输模式...";
  
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
  
  // Then, publish event for SystemManager to handle
  DBG.println(F("[WebServer] Publishing EVT_BMS_SHIPMODE_REQUEST event"));
  EventBus::getInstance().publish(EVT_BMS_SHIPMODE_REQUEST, nullptr);
}

void WebServer::handleBmsResetData(AsyncWebServerRequest* request) {
  if (!systemManager) {
    request->send(503, "application/json", "{\"success\":false,\"message\":\"系统未就绪\"}");
    return;
  }
  DBG.println(F("[WebServer] BMS Reset Battery Data request received"));

  StaticJsonDocument<256> doc;
  doc["success"] = true;
  doc["message"] = "请求已接收，正在重置电池数据...";

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);

  DBG.println(F("[WebServer] Publishing EVT_BMS_RESET_BATTERY_DATA event"));
  EventBus::getInstance().publish(EVT_BMS_RESET_BATTERY_DATA, nullptr);
}

void WebServer::handleClearTips(AsyncWebServerRequest* request) {
  if (systemManager) {
    systemManager->clearTips();
    notifyClients();
  }
  request->send(200, "application/json", "{\"success\":true}");
}

void WebServer::handleRestart(AsyncWebServerRequest* request) {
  if (systemManager == nullptr) {
    sendErrorResponse(request, "Config mode", 503);
    return;
  }

  const System_Global_State& state = systemManager->getGlobalState();
  if (state.power_mode != POWER_MODE_AC) {
    sendErrorResponse(request, "AC power required for restart", 403);
    return;
  }

  StaticJsonDocument<128> doc;
  doc["success"] = true;
  doc["message"] = "Device will reboot in 2 seconds";
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);

  DBG.println(F("Restart requested via web API. Rebooting in 2 seconds..."));

  static Ticker restartTicker;
  restartTicker.once(2, []() {
    DBG.println(F("Rebooting now..."));
    ESP.restart();
  });
}

// =============================================================================
// Generic File Handlers
// =============================================================================

extern SemaphoreHandle_t g_spiffs_mutex;

void WebServer::handleFileList(AsyncWebServerRequest* request, const char* dirPath) {
  if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);

  File root = SPIFFS.open(dirPath);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
    request->send(200, "application/json", "{\"success\":true,\"files\":[]}");
    return;
  }

  DynamicJsonDocument doc(2048);
  doc["success"] = true;
  JsonArray files = doc.createNestedArray("files");

  size_t dirPathLen = strlen(dirPath);
  File f = root.openNextFile();
  int idx = 0;
  while (f && idx < 50) {
    const char* name = f.name();
    // 跳过目录自身
    if (strcmp(name, dirPath) != 0) {
      JsonObject obj = files.createNestedObject();
      // 兼容不同 ESP32 核心版本：确保文件名带目录前缀
      if (strncmp(name, dirPath, dirPathLen) == 0 && name[dirPathLen] == '/') {
        obj["name"] = name;
      } else {
        obj["name"] = String(dirPath) + "/" + name;
      }
      obj["size"] = f.size();
      idx++;
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();

  if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServer::handleFileDownload(AsyncWebServerRequest* request, const char* allowedDir) {
  if (!request->hasParam("name")) {
    sendErrorResponse(request, "Missing 'name' parameter", 400);
    return;
  }

  String filename = request->getParam("name")->value();

  // 安全校验：只允许指定目录下的文件
  if (filename.indexOf("..") >= 0 || filename.indexOf(allowedDir) != 0) {
    sendErrorResponse(request, "Invalid filename", 403);
    return;
  }

  if (g_spiffs_mutex) xSemaphoreTake(g_spiffs_mutex, portMAX_DELAY);

  File file = SPIFFS.open(filename, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);
    sendErrorResponse(request, "File not found", 404);
    return;
  }

  size_t fileSize = file.size();

  // 提取不含路径的文件名用于 Content-Disposition
  const char* baseName = strrchr(filename.c_str(), '/');
  baseName = baseName ? baseName + 1 : filename.c_str();

  // File 通过 shared_ptr 引用计数，lambda 持有副本可保持文件打开
  // 响应发送完成后 response 析构 → lambda 析构 → File 析构 → 自动关闭
  AsyncWebServerResponse* response = request->beginResponse(
    "application/octet-stream", fileSize,
    [file](uint8_t* buffer, size_t maxLen, size_t index) mutable -> size_t {
      size_t remaining = file.size() - index;
      if (remaining == 0) return 0;
      size_t toRead = (maxLen < remaining) ? maxLen : remaining;
      return file.read(buffer, toRead);
    }
  );
  String disposition = String("attachment; filename=\"") + baseName + "\"";
  response->addHeader("Content-Disposition", disposition);

  // 立即释放 SPIFFS 互斥锁，文件通过 File 引用计数保持打开
  if (g_spiffs_mutex) xSemaphoreGive(g_spiffs_mutex);

  request->send(response);
}

// =============================================================================
// Raw Data File Handlers
// =============================================================================

void WebServer::handleRawFileList(AsyncWebServerRequest* request) {
  handleFileList(request, "/raw");
}

void WebServer::handleRawFileDownload(AsyncWebServerRequest* request) {
  handleFileDownload(request, "/raw/");
}

// =============================================================================
// Log File Handlers
// =============================================================================

void WebServer::handleLogFileList(AsyncWebServerRequest* request) {
  handleFileList(request, "/log");
}

void WebServer::handleLogFileDownload(AsyncWebServerRequest* request) {
  handleFileDownload(request, "/log/");
}

// =============================================================================
// ADC Calibration API Handlers
// =============================================================================

void WebServer::handleCalibrationGet(AsyncWebServerRequest* request) {
  if (systemManager == nullptr || systemManager->getADCCalibration() == nullptr) {
    sendConfigModeResponse(request);
    return;
  }

  StaticJsonDocument<256> doc;
  doc["success"] = true;

  const uint8_t* cal = systemManager->getADCCalibration();
  JsonArray arr = doc.createNestedArray("calibration");
  for (uint8_t i = 0; i < ADC_CAL_PIN_COUNT; i++) {
    arr.add(cal[i]);
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServer::handleCalibrationPost(AsyncWebServerRequest* request) {
  if (systemManager == nullptr) {
    sendErrorResponse(request, "Config mode", 503);
    return;
  }

  String jsonString;
  if (!takeBody(request, jsonString)) {
    sendErrorResponse(request, "Missing request body", 400);
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    sendErrorResponse(request, "Invalid JSON format", 400);
    return;
  }

  if (!doc.containsKey("calibration")) {
    sendErrorResponse(request, "Missing calibration array", 400);
    return;
  }

  JsonArrayConst calArray = doc["calibration"];
  if (calArray.size() != ADC_CAL_PIN_COUNT) {
    sendErrorResponse(request, "Invalid calibration array size", 400);
    return;
  }

  for (uint8_t i = 0; i < ADC_CAL_PIN_COUNT; i++) {
    systemManager->setADCCalibration(ADC_CAL_PINS[i], (uint8_t)calArray[i]);
  }

  StaticJsonDocument<128> resp;
  resp["success"] = true;
  resp["message"] = "Calibration saved";

  String response;
  serializeJson(resp, response);
  request->send(200, "application/json", response);

  DBG.println(F("[WebServer] ADC calibration updated"));
}

// =============================================================================
// OTA 版本解析与比对辅助函数
// =============================================================================

// 从缓冲区中提取标签后的版本字符串，返回写入 outVer 的字符数，0 表示未找到
// 格式："...SIG:UPS-ESP32S3:VER:1.0.2..." -> 提取 "1.0.2"
static size_t parseVersionFromTag(const uint8_t* buf, size_t bufLen,
                                  const char* prefix, char* outVer, size_t outSize) {
  size_t prefixLen = strlen(prefix);
  // 在 buf 中搜索 prefix (不依赖 memmem)
  for (size_t i = 0; i + prefixLen <= bufLen; i++) {
    if (memcmp(buf + i, prefix, prefixLen) == 0) {
      // prefix 之后即版本号，读取直到分隔符
      const uint8_t* verStart = buf + i + prefixLen;
      size_t j = 0;
      while (j < outSize - 1 && (i + prefixLen + j) < bufLen) {
        char c = (char)verStart[j];
        if (c == ' ' || c == '"' || c == '\n' || c == '\r' || c == '\0') break;
        outVer[j] = c;
        j++;
      }
      outVer[j] = '\0';
      return j;
    }
  }
  return 0;
}

// x.y.z 格式版本号比较：newVer >= minVer 返回 true
static bool isVersionGreaterOrEqual(const char* newVer, const char* minVer) {
  int nMajor = 0, nMinor = 0, nPatch = 0;
  int mMajor = 0, mMinor = 0, mPatch = 0;
  sscanf(newVer, "%d.%d.%d", &nMajor, &nMinor, &nPatch);
  sscanf(minVer,  "%d.%d.%d", &mMajor, &mMinor, &mPatch);
  if (nMajor != mMajor) return nMajor > mMajor;
  if (nMinor != mMinor) return nMinor > mMinor;
  return nPatch >= mPatch;
}

// =============================================================================
// OTA Update Handlers
// =============================================================================

void WebServer::handleFirmwareUpload(AsyncWebServerRequest* request, String filename,
                                       size_t index, uint8_t* data, size_t len, bool final) {
  // 二阶段 OTA：先缓冲并搜索签名+版本号，验证通过后再流式写入 flash
  // 签名在固件中的位置取决于链接器 .rodata 放置，可能不在前几 KB

  static bool    otaVerified = false;
  static bool    otaRejected = false;
  static size_t  lastPrint = 0;
  static size_t  totalBytes = 0;
  static uint8_t otaBuf[8192];
  static size_t  otaBufLen = 0;

  const size_t prefixLen = strlen(EXPECTED_SIG_PREFIX);

  // 认证检查必须在上传处理器中进行：上传体处理器先于请求完成处理器执行，
  // final 分块会调用 Update.end(true) 切换启动分区，仅在完成处理器中鉴权
  // 无法阻止未认证的固件写入。isAuthenticated 支持 Cookie 会话与 Basic Auth
  if (!isAuthenticated(request)) {
    if (!index) DBG.println(F("OTA REJECT: 未认证的固件上传请求"));
    return;
  }

  if (!index) {
    otaVerified = false;
    otaRejected = false;
    lastPrint = 0;
    totalBytes = 0;
    otaBufLen = 0;
    s_otaSuccess = false;

    uint32_t maxSketchSpace = ESP.getFreeSketchSpace();
    if (maxSketchSpace == 0) {
      DBG.println(F("OTA ERROR: 无法获取可用 Flash 空间"));
      otaRejected = true;
      return;
    }
    uint32_t safeSize = maxSketchSpace - 0x1000;

    // OTA前持久化所有模块数据
    if (systemManager) {
      systemManager->saveAllData();
    }

    if (!Update.begin(safeSize)) {
      DBG.print(F("OTA ERROR: Update.begin() 失败："));
      DBG.println(Update.errorString());
      otaRejected = true;
      return;
    }
  }

  if (otaRejected) return;
  if (len == 0 && !final) return;

  // === 阶段 A：签名验证前，缓冲数据 ===
  if (!otaVerified) {
    // 追加到缓冲区（不超过 8KB）
    size_t remaining = sizeof(otaBuf) - otaBufLen;
    size_t copyLen = (len < remaining) ? len : remaining;
    if (copyLen > 0) memcpy(otaBuf + otaBufLen, data, copyLen);
    otaBufLen += copyLen;
    totalBytes += len;  // 包括溢出部分

    // 每次追加后在已缓冲数据中搜索签名
    for (size_t i = 0; i + prefixLen + 1 <= otaBufLen; i++) {
      if (memcmp(otaBuf + i, EXPECTED_SIG_PREFIX, prefixLen) != 0) continue;

      // 找到前缀，打印周围字节用于调试
      size_t dumpStart = (i >= 8) ? i - 8 : 0;
      size_t dumpEnd = i + prefixLen + 16;
      if (dumpEnd > otaBufLen) dumpEnd = otaBufLen;

      // 提取版本号
      char newVer[16] = {0};
      size_t j = 0;
      const uint8_t* verStart = otaBuf + i + prefixLen;
      size_t verAvail = otaBufLen - i - prefixLen;
      while (j < sizeof(newVer) - 1 && j < verAvail) {
        char c = (char)verStart[j];
        if (c == ' ' || c == '"' || c == '\n' || c == '\r' || c == '\0') break;
        newVer[j] = c;
        j++;
      }
      newVer[j] = '\0';

      if (j == 0) {
        DBG.printf_P(PSTR("OTA: 前缀已找到(偏移 %u, bufLen=%u)，等待版本号数据...\n"), (unsigned)i, (unsigned)otaBufLen);
        break;
      }

      // 有版本号，开始校验
      if (!isVersionGreaterOrEqual(newVer, MIN_REQUIRED_VERSION)) {
        DBG.printf_P(PSTR("OTA REJECT: 版本 %s < 最低要求 %s\n"), newVer, MIN_REQUIRED_VERSION);
        otaRejected = true;
        Update.abort();
        return;
      }

      // 版本校验通过，将缓冲区写入 flash
      if (Update.write(otaBuf, otaBufLen) != otaBufLen) {
        DBG.print(F("OTA ERROR: 缓冲区写入失败："));
        DBG.println(Update.errorString());
        otaRejected = true;
        return;
      }

      // 写入当前 chunk 中超出缓冲区的部分
      if (len > copyLen) {
        size_t extraOffset = copyLen;
        size_t extraLen = len - extraOffset;
        if (Update.write(data + extraOffset, extraLen) != extraLen) {
          DBG.print(F("OTA ERROR: 额外数据写入失败："));
          DBG.println(Update.errorString());
          otaRejected = true;
          return;
        }
      }

      otaVerified = true;
      lastPrint = totalBytes / 1024;
      break;
    }

    if (!otaVerified && otaBufLen >= sizeof(otaBuf) && !final) {
      DBG.printf_P(PSTR("OTA REJECT: 缓冲区已满(%u 字节)未找到签名\n"), (unsigned)otaBufLen);
      otaRejected = true;
      Update.abort();
    }

    if (final && !otaVerified && !otaRejected) {
      DBG.println(F("OTA REJECT: 全文未找到项目特征前缀。"));
      otaRejected = true;
      Update.abort();
    }
    return;
  }

  // === 阶段 B：签名已验证，流式写入 ===
  if (len && Update.write(data, len) != len) {
    DBG.print(F("OTA ERROR: 写入失败："));
    DBG.println(Update.errorString());
    otaRejected = true;
    return;
  }
  totalBytes += len;

  if (totalBytes - lastPrint > 51200) {
    lastPrint = totalBytes / 1024;
  }

  if (final) {
    DBG.printf_P(PSTR("OTA: 写入完成，共 %lu KB\n"), totalBytes / 1024);
    if (Update.end(true)) {
      s_otaSuccess = true;
      DBG.println(F("OTA 成功！即将重启..."));
      const esp_partition_t* bootPart = esp_ota_get_boot_partition();
      if (bootPart) DBG.printf_P(PSTR("OTA: 启动分区已切换至 %s @ 0x%06X\n"), bootPart->label, bootPart->address);
    } else {
      DBG.print(F("OTA ERROR: 结束失败："));
      DBG.println(Update.errorString());
    }
  }
}