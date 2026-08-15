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
#include "webhook_manager.h"
#include <esp_ota_ops.h>
#include <Ticker.h>
#include <SPIFFS.h>

extern WebhookManager* webhookManager;

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
// 上限 8KB：/save 最大合法请求 ~3KB，超限直接停止累积（login/setup 免认证，需防内存耗尽）
static const size_t BODY_MAX_LEN = 8192;
static void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
  if (!request->_tempObject) {
    request->_tempObject = new String();
    ((String*)request->_tempObject)->reserve(512);
  }
  String* body = (String*)request->_tempObject;
  if (body->length() >= BODY_MAX_LEN) return;
  if (body->length() + len > BODY_MAX_LEN) len = BODY_MAX_LEN - body->length();
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

  // ---- Webhook API (认证) ----
  onAuth("/api/webhook", HTTP_GET, [this](AsyncWebServerRequest* r) { handleWebhookGet(r); });
  // 注意：/api/webhook/test 必须先于 /api/webhook 注册，
  // 否则 POST /api/webhook/test 会被 /api/webhook 的保存路由抢先匹配，导致测试发送失效
  onAuthBody("/api/webhook/test", [this](AsyncWebServerRequest* r) { handleWebhookTest(r); });
  onAuthBody("/api/webhook", [this](AsyncWebServerRequest* r) { handleWebhookPost(r); });

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

void WebServer::cleanupWsClients() {
  ws.cleanupClients();
}

void WebServer::notifyClients() {
  ws.cleanupClients();  // 回收断开的 WebSocket 客户端，防止内存泄漏

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
  doc["chemistry"] = getChemistryLimits((BatteryChemistry_t)state.bms.chemistry).name;
  doc["soc_error_est"] = serialized(String(state.bms.soc_error_est, 1));
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
  
  // 预分配缓冲区，避免反复 String 临时对象 + 拼接导致堆碎片化
  String metrics;
  metrics.reserve(4096);
  char numBuf[32];  // 数字→字符串转换复用缓冲区

  #define MET(s) metrics += s
  #define MET_NUM(label, val) do { snprintf(numBuf, sizeof(numBuf), "%.2f", (double)(val)); metrics += label; metrics += numBuf; metrics += "\n\n"; } while(0)
  #define MET_INT(label, val) do { snprintf(numBuf, sizeof(numBuf), "%d", (int)(val)); metrics += label; metrics += numBuf; metrics += "\n\n"; } while(0)
  #define MET_ULONG(label, val) do { snprintf(numBuf, sizeof(numBuf), "%lu", (unsigned long)(val)); metrics += label; metrics += numBuf; metrics += "\n\n"; } while(0)
  #define MET_BOOL(label, val) do { metrics += label; metrics += (val ? "1" : "0"); metrics += "\n\n"; } while(0)
  
  // System metrics
  MET("# HELP ups_system_uptime System uptime in seconds\n"
      "# TYPE ups_system_uptime gauge\nups_system_uptime ");
  MET_ULONG("", state.system.uptime);
  
  MET("# HELP ups_system_overall_status Overall system status (0=normal, 1=warning, 2=fault)\n"
      "# TYPE ups_system_overall_status gauge\nups_system_overall_status ");
  MET_INT("", state.overall_status);
  
  MET("# HELP ups_system_power_mode Power mode (0=AC, 1=BATTERY, 2=HYBRID, 3=CHARGING)\n"
      "# TYPE ups_system_power_mode gauge\nups_system_power_mode ");
  MET_INT("", state.power_mode);
  
  MET("# HELP ups_system_emergency_shutdown Emergency shutdown status (0=normal, 1=shutdown)\n"
      "# TYPE ups_system_emergency_shutdown gauge\nups_system_emergency_shutdown ");
  MET_BOOL("", state.emergency_shutdown);
  
  MET("# HELP ups_system_board_temperature Board temperature in Celsius\n"
      "# TYPE ups_system_board_temperature gauge\nups_system_board_temperature ");
  MET_NUM("", state.system.board_temperature);
  
  MET("# HELP ups_system_environment_temperature Environment temperature in Celsius\n"
      "# TYPE ups_system_environment_temperature gauge\nups_system_environment_temperature ");
  MET_NUM("", state.system.environment_temperature);

  MET("# HELP ups_system_board_temperature_sht SHTC3 board temperature in Celsius\n"
      "# TYPE ups_system_board_temperature_sht gauge\nups_system_board_temperature_sht ");
  MET_NUM("", state.system.board_temperature_sht);

  MET("# HELP ups_system_board_humidity SHTC3 board humidity in percent\n"
      "# TYPE ups_system_board_humidity gauge\nups_system_board_humidity ");
  MET_NUM("", state.system.board_humidity);
  
  MET("# HELP ups_system_wifi_connected WiFi connection status (0=disconnected, 1=connected)\n"
      "# TYPE ups_system_wifi_connected gauge\nups_system_wifi_connected ");
  MET_BOOL("", state.system.wifi_connected);
  
  MET("# HELP ups_system_wifi_rssi WiFi signal strength in dBm\n"
      "# TYPE ups_system_wifi_rssi gauge\nups_system_wifi_rssi ");
  MET_INT("", state.system.wifi_rssi);
  
  MET("# HELP ups_system_wifi_status WiFi status code\n"
      "# TYPE ups_system_wifi_status gauge\nups_system_wifi_status ");
  MET_INT("", state.system.wifi_status);
  
  MET("# HELP ups_system_led_brightness LED brightness (0-255)\n"
      "# TYPE ups_system_led_brightness gauge\nups_system_led_brightness ");
  MET_INT("", state.system.led_brightness);
  
  MET("# HELP ups_system_buzzer_enabled Buzzer enabled status (0=disabled, 1=enabled)\n"
      "# TYPE ups_system_buzzer_enabled gauge\nups_system_buzzer_enabled ");
  MET_BOOL("", state.system.buzzer_enabled);
  
  MET("# HELP ups_system_buzzer_volume Buzzer volume (0-255)\n"
      "# TYPE ups_system_buzzer_volume gauge\nups_system_buzzer_volume ");
  MET_INT("", state.system.buzzer_volume);
  
  MET("# HELP ups_system_hardware_version Hardware version\n"
      "# TYPE ups_system_hardware_version gauge\nups_system_hardware_version{version=\"");
  MET(state.system.hardware_version);
  MET("\"} 1\n\n");
  
  // BMS metrics
  MET("# HELP ups_bms_soc Battery state of charge percentage\n"
      "# TYPE ups_bms_soc gauge\nups_bms_soc ");
  MET_NUM("", state.bms.soc);
  
  MET("# HELP ups_bms_soh Battery state of health percentage\n"
      "# TYPE ups_bms_soh gauge\nups_bms_soh ");
  MET_NUM("", state.bms.soh);
  
  MET("# HELP ups_bms_voltage Battery voltage in millivolts\n"
      "# TYPE ups_bms_voltage gauge\nups_bms_voltage ");
  MET_INT("", state.bms.voltage);
  
  MET("# HELP ups_bms_current Battery current in milliamperes (positive=charging, negative=discharging)\n"
      "# TYPE ups_bms_current gauge\nups_bms_current ");
  MET_INT("", state.bms.current);
  
  MET("# HELP ups_bms_temperature Battery temperature in Celsius\n"
      "# TYPE ups_bms_temperature gauge\nups_bms_temperature ");
  MET_NUM("", state.bms.temperature);
  
  MET("# HELP ups_bms_cycle_count Battery cycle count\n"
      "# TYPE ups_bms_cycle_count gauge\nups_bms_cycle_count ");
  MET_INT("", state.bms.cycle_count);

  MET("# HELP ups_bms_chemistry Battery chemistry (0=NCM, 1=LiFePO4)\n"
      "# TYPE ups_bms_chemistry gauge\nups_bms_chemistry ");
  MET_INT("", state.bms.chemistry);

  MET("# HELP ups_bms_soc_error_est Estimated SOC error budget in percent\n"
      "# TYPE ups_bms_soc_error_est gauge\nups_bms_soc_error_est ");
  snprintf(numBuf, sizeof(numBuf), "%.1f", state.bms.soc_error_est);
  MET(numBuf); MET("\n\n");
  
  MET("# HELP ups_bms_capacity_full Full battery capacity in mAh\n"
      "# TYPE ups_bms_capacity_full gauge\nups_bms_capacity_full ");
  MET_INT("", state.bms.capacity_full);
  
  MET("# HELP ups_bms_capacity_remaining Remaining battery capacity in mAh\n"
      "# TYPE ups_bms_capacity_remaining gauge\nups_bms_capacity_remaining ");
  MET_INT("", state.bms.capacity_remaining);
  
  MET("# HELP ups_bms_connected BMS connection status (0=disconnected, 1=connected)\n"
      "# TYPE ups_bms_connected gauge\nups_bms_connected ");
  MET_BOOL("", state.bms.is_connected);
  
  MET("# HELP ups_bms_balancing_active Cell balancing active status (0=inactive, 1=active)\n"
      "# TYPE ups_bms_balancing_active gauge\nups_bms_balancing_active ");
  MET_BOOL("", state.bms.balancing_active);
  
  MET("# HELP ups_bms_fault_type BMS fault type (0=none, see BMS_Fault_t enum)\n"
      "# TYPE ups_bms_fault_type gauge\nups_bms_fault_type ");
  MET_INT("", state.bms.fault_type);
  
  // Cell voltages
  MET("# HELP ups_bms_cell_voltage Individual cell voltage in millivolts\n"
      "# TYPE ups_bms_cell_voltage gauge\n");
  for (int i = 0; i < 5; i++) {
    MET("ups_bms_cell_voltage{cell=\"");
    snprintf(numBuf, sizeof(numBuf), "%d\"} %d\n", i + 1, state.bms.cell_voltages[i]);
    MET(numBuf);
  }
  MET("\n");
  
  MET("# HELP ups_bms_cell_voltage_min Minimum cell voltage in millivolts\n"
      "# TYPE ups_bms_cell_voltage_min gauge\nups_bms_cell_voltage_min ");
  MET_INT("", state.bms.cell_voltage_min);
  
  MET("# HELP ups_bms_cell_voltage_max Maximum cell voltage in millivolts\n"
      "# TYPE ups_bms_cell_voltage_max gauge\nups_bms_cell_voltage_max ");
  MET_INT("", state.bms.cell_voltage_max);
  
  MET("# HELP ups_bms_cell_voltage_avg Average cell voltage in millivolts\n"
      "# TYPE ups_bms_cell_voltage_avg gauge\nups_bms_cell_voltage_avg ");
  MET_INT("", state.bms.cell_voltage_avg);

  // Internal resistance
  MET("# HELP ups_bms_cell_ir Estimated cell internal resistance in mΩ\n"
      "# TYPE ups_bms_cell_ir gauge\n");
  for (int i = 0; i < 5; i++) {
    MET("ups_bms_cell_ir{cell=\"");
    snprintf(numBuf, sizeof(numBuf), "%d\"} %.1f\n", i + 1, state.bms.cell_internal_resistance[i]);
    MET(numBuf);
  }
  MET("\n");

  MET("# HELP ups_bms_ir_sample_count Number of internal resistance measurements\n"
      "# TYPE ups_bms_ir_sample_count gauge\nups_bms_ir_sample_count ");
  MET_INT("", state.bms.ir_sample_count);
  
  // Power metrics
  MET("# HELP ups_power_input_voltage Input voltage in millivolts\n"
      "# TYPE ups_power_input_voltage gauge\nups_power_input_voltage ");
  MET_INT("", state.power.input_voltage);
  
  MET("# HELP ups_power_input_current Input current in milliamperes\n"
      "# TYPE ups_power_input_current gauge\nups_power_input_current ");
  MET_INT("", state.power.input_current);
  
  MET("# HELP ups_power_output_power Output power in milliwatts\n"
      "# TYPE ups_power_output_power gauge\nups_power_output_power ");
  MET_INT("", state.power.output_power);
  
  MET("# HELP ups_power_battery_voltage Battery voltage in millivolts\n"
      "# TYPE ups_power_battery_voltage gauge\nups_power_battery_voltage ");
  MET_INT("", state.power.battery_voltage);
  
  MET("# HELP ups_power_battery_current Battery current in milliamperes\n"
      "# TYPE ups_power_battery_current gauge\nups_power_battery_current ");
  MET_INT("", state.power.battery_current);
  
  MET("# HELP ups_power_ac_present AC power present status (0=absent, 1=present)\n"
      "# TYPE ups_power_ac_present gauge\nups_power_ac_present ");
  MET_BOOL("", state.power.ac_present);
  
  MET("# HELP ups_power_charger_enabled Charger enabled status (0=disabled, 1=enabled)\n"
      "# TYPE ups_power_charger_enabled gauge\nups_power_charger_enabled ");
  MET_BOOL("", state.power.charger_enabled);
  
  MET("# HELP ups_power_hybrid_mode Hybrid mode status (0=disabled, 1=enabled)\n"
      "# TYPE ups_power_hybrid_mode gauge\nups_power_hybrid_mode ");
  MET_BOOL("", state.power.hybrid_mode);
  
  MET("# HELP ups_power_fault_type Power fault type (0=none, see Power_Fault_Type_t enum)\n"
      "# TYPE ups_power_fault_type gauge\nups_power_fault_type ");
  MET_INT("", state.power.fault_type);
  
  MET("# HELP ups_power_bq24780s_connected BQ24780S/BQ24800 chip connection status (0=disconnected, 1=connected)\n"
      "# TYPE ups_power_bq24780s_connected gauge\nups_power_bq24780s_connected ");
  MET_BOOL("", state.power.bq24780s_connected);

  MET("# HELP ups_power_chip_variant Charger chip variant (0=BQ24780S, 1=BQ24800)\n"
      "# TYPE ups_power_chip_variant gauge\nups_power_chip_variant ");
  MET_INT("", state.power.chip_variant);
  
  MET("# HELP ups_power_prochot_status PROCHOT pin status (0=normal, 1=triggered)\n"
      "# TYPE ups_power_prochot_status gauge\nups_power_prochot_status ");
  MET_BOOL("", state.power.prochot_status);
  
  MET("# HELP ups_power_tbstat_status TB_STAT pin status (0=normal, 1=triggered)\n"
      "# TYPE ups_power_tbstat_status gauge\nups_power_tbstat_status ");
  MET_BOOL("", state.power.tbstat_status);
  
  // Protection status
  MET("# HELP ups_protection_over_current Over-current protection status (0=normal, 1=triggered)\n"
      "# TYPE ups_protection_over_current gauge\nups_protection_over_current ");
  MET_BOOL("", state.over_current_protection);
  
  MET("# HELP ups_protection_over_temp Over-temperature protection status (0=normal, 1=triggered)\n"
      "# TYPE ups_protection_over_temp gauge\nups_protection_over_temp ");
  MET_BOOL("", state.over_temp_protection);
  
  MET("# HELP ups_protection_short_circuit Short-circuit protection status (0=normal, 1=triggered)\n"
      "# TYPE ups_protection_short_circuit gauge\nups_protection_short_circuit ");
  MET_BOOL("", state.short_circuit_protection);

  // Self-consumption metrics
  MET("# HELP ups_self_consumption_mA System self-consumption current in mA (0=not calculated)\n"
      "# TYPE ups_self_consumption_mA gauge\nups_self_consumption_mA ");
  snprintf(numBuf, sizeof(numBuf), "%.2f", state.self_consumption_mA);
  MET(numBuf); MET("\n\n");

  MET("# HELP ups_sc_segment_count Number of valid quiescent segments used\n"
      "# TYPE ups_sc_segment_count gauge\nups_sc_segment_count ");
  MET_INT("", state.sc_segment_count);

  MET("# HELP ups_sc_total_segments Total quiescent segments found (including invalid)\n"
      "# TYPE ups_sc_total_segments gauge\nups_sc_total_segments ");
  MET_INT("", state.sc_total_segments);

  MET("# HELP ups_sc_last_check Last self-consumption analysis check time (Unix timestamp)\n"
      "# TYPE ups_sc_last_check gauge\nups_sc_last_check ");
  MET_ULONG("", state.sc_last_check);

  #undef MET
  #undef MET_NUM
  #undef MET_INT
  #undef MET_ULONG
  #undef MET_BOOL

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
  // 请求体大小检查：防止未认证客户端发送超大 body 耗尽内存
  if (request->_tempObject && ((String*)request->_tempObject)->length() >= BODY_MAX_LEN) {
    sendErrorResponse(request, "Request body too large", 413);
    discardBody(request);
    return;
  }
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
  // 请求体大小检查
  if (request->_tempObject && ((String*)request->_tempObject)->length() >= BODY_MAX_LEN) {
    sendErrorResponse(request, "Request body too large", 413);
    discardBody(request);
    return;
  }
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

// HTML 属性值转义（防 XSS）：仅处理 " < > &
static void escapeHtmlAttr(const char* in, char* out, size_t outSize) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 6 < outSize; i++) {
    char c = in[i];
    if      (c == '"')  { memcpy(out+j, "&quot;", 6); j += 6; }
    else if (c == '<')  { memcpy(out+j, "&lt;", 4);   j += 4; }
    else if (c == '>')  { memcpy(out+j, "&gt;", 4);   j += 4; }
    else if (c == '&')  { memcpy(out+j, "&amp;", 5);  j += 5; }
    else { out[j++] = c; }
  }
  out[j] = '\0';
}

void WebServer::renderSPA(AsyncWebServerRequest* request) {
  const Configuration* sysConfig = configManager->getSystemConfig();
  const BMS_Config_t* bmsConfig = configManager->getBMSConfig();
  const Power_Config_t* powerConfig = configManager->getPowerConfig();

  bool isConfigMode = (systemManager == nullptr);
  const System_Global_State* statePtr = nullptr;
  if (!isConfigMode) {
    statePtr = &systemManager->getGlobalState();
  }

  // CSS（从 PROGMEM 拼接到 RAM）
  size_t cssLen = strlen_P(COMMON_CSS) + strlen_P(CONFIG_CSS) + strlen_P(OTA_CSS) + strlen_P(WIZARD_CSS);
  char* cssBuf = new char[cssLen + 1];
  cssBuf[0] = '\0';
  strcat_P(cssBuf, COMMON_CSS);
  strcat_P(cssBuf, CONFIG_CSS);
  strcat_P(cssBuf, OTA_CSS);
  strcat_P(cssBuf, WIZARD_CSS);
  // JS
  size_t jsLen = strlen_P(SPA_PAGE_JS);
  char* jsBuf = new char[jsLen + 1];
  jsBuf[0] = '\0';
  strcat_P(jsBuf, SPA_PAGE_JS);
  // 充电窗口 + cell_count 注入脚本
  String windowsJson = isConfigMode ? "[]" : buildChargingWindowsJson(powerConfig->charging_windows, powerConfig->charging_window_count);
  size_t injLen = windowsJson.length() + 128;
  char* injectBuf = new char[injLen];
  snprintf(injectBuf, injLen,
    "<script>window.CONFIG_MODE=%d;window.CURLANG='%s';window.CELL_COUNT=%d;window.IW=%s;</script>",
    isConfigMode ? 1 : 0, I18n::getLangCode(),
    isConfigMode ? 3 : bmsConfig->cell_count,
    windowsJson.c_str());

  // 临时缓冲区（esc 需容纳最长字段 64 字符全转义的最坏情况 64×6=384）
  char esc[512];
  char tmp[64];

  // ---- 将模板从 PROGMEM 复制到 RAM，后续用 strstr 做占位符匹配 ----
  size_t tplLen = strlen_P(SPA_PAGE_TEMPLATE);
  char* tpl = new char[tplLen + 1];
  memcpy_P(tpl, SPA_PAGE_TEMPLATE, tplLen + 1);

  // 输出缓冲区：模板 + CSS/JS + 替换值余量
  size_t outCap = tplLen + cssLen + jsLen + 16384;
  char* out = new char[outCap];
  size_t j = 0;

  // 追加辅助宏（C++11 兼容，不使用泛型 lambda）
  #define AP_S(s) do { size_t _l = strlen(s); if (j + _l < outCap) { memcpy(out+j, s, _l); j += _l; } } while(0)
  #define AP_V(s) do { escapeHtmlAttr(s, esc, sizeof(esc)); AP_S(esc); } while(0)
  #define AP_N(v) do { snprintf(tmp, sizeof(tmp), "%d", (int)(v)); AP_S(tmp); } while(0)
  #define AP_F(v) do { snprintf(tmp, sizeof(tmp), "%.1f", (float)(v)); AP_S(tmp); } while(0)
  #define AP_L(v) do { snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)(v)); AP_S(tmp); } while(0)

  // 单遍扫描：从 pos 开始，找到下一个 % 或特殊标签，输出前缀 + 替换值
  const char* pos = tpl;

  while (*pos && j + 512 < outCap) {
    // 检查特殊标签：匹配开标签，注入完整内容，闭合标签自然跳过
    if (pos[0] == '<') {
      if (strncmp(pos, "<style id=\"dynamic-css\">", 24) == 0) {
        AP_S("<style>"); AP_S(cssBuf); AP_S("</style>");
        pos += 32; continue;
      }
      if (strncmp(pos, "<script id=\"dynamic-js\">", 24) == 0) {
        AP_S("<script>"); AP_S(jsBuf); AP_S("</script>");
        pos += 33; continue;
      }
      if (strncmp(pos, "</head>", 7) == 0) {
        AP_S(injectBuf); AP_S("</head>");
        pos += 7; continue;
      }
    }

    // 检查 %...% 占位符
    if (pos[0] == '%') {
      // 匹配辅助：检查 pos 是否以 "%name%" 开头，是则执行 body 并 continue
      #define TRY_PH(name, body) \
        if (strncmp(pos, "%" name "%", strlen(name) + 2) == 0) { body; pos += strlen(name) + 2; continue; }

      // --- 系统配置 ---
      TRY_PH("WIFI_SSID",          AP_V(sysConfig->wifi_ssid))
      TRY_PH("WIFI_PASS",           AP_V(sysConfig->wifi_pass[0] ? sysConfig->wifi_pass : ""))
      TRY_PH("BUZZER_STATUS",       AP_S(sysConfig->buzzer_enabled ? I18n::get(STR_ENABLED) : I18n::get(STR_DISABLED)))
      TRY_PH("BUZZER_CHECKED",      AP_S(sysConfig->buzzer_enabled ? "checked" : ""))
      TRY_PH("VOLUME_VALUE",        AP_N(sysConfig->buzzer_volume))
      TRY_PH("VOLUME_LEVEL",        do { snprintf(tmp, sizeof(tmp), "%d%%", sysConfig->buzzer_volume); AP_S(tmp); } while(0))
      TRY_PH("LIGHT_VALUE",         AP_N(sysConfig->led_brightness))
      TRY_PH("LIGHT_BRIGHTNESS",    do { snprintf(tmp, sizeof(tmp), "%d%%", sysConfig->led_brightness); AP_S(tmp); } while(0))
      TRY_PH("HID_CHECKED",         AP_S(sysConfig->hid_enabled ? "checked" : ""))
      TRY_PH("HID_MODE_MAH",        AP_S(sysConfig->hid_report_mode == 0 ? " selected" : ""))
      TRY_PH("HID_MODE_MWH",        AP_S(sysConfig->hid_report_mode == 1 ? " selected" : ""))
      TRY_PH("HID_MODE_PCT",        AP_S(sysConfig->hid_report_mode == 2 ? " selected" : ""))
      TRY_PH("MQTT_CHECKED",        AP_S((sysConfig->mqtt_broker[0] != '\0' && sysConfig->mqtt_port > 0) ? "checked" : ""))
      TRY_PH("MQTT_BROKER",         AP_V(sysConfig->mqtt_broker))
      TRY_PH("MQTT_PORT",           AP_N(sysConfig->mqtt_port))
      TRY_PH("MQTT_USERNAME",       AP_V(sysConfig->mqtt_username))
      TRY_PH("MQTT_PASSWORD",       AP_V(sysConfig->mqtt_password))
      TRY_PH("XIAOMI_CHECKED",      AP_S(sysConfig->xiaomi_sensor_enabled ? "checked" : ""))
      TRY_PH("XIAOMI_SECTION_DISPLAY", AP_S(g_is_new_board ? "block" : "none"))
      TRY_PH("IP_MODE_DHCP",        AP_S(sysConfig->use_static_ip ? "" : " selected"))
      TRY_PH("IP_MODE_STATIC",      AP_S(sysConfig->use_static_ip ? " selected" : ""))
      TRY_PH("STATIC_IP_DISPLAY",   AP_S(sysConfig->use_static_ip ? "block" : "none"))
      TRY_PH("STATIC_IP",           AP_V(sysConfig->static_ip))
      TRY_PH("STATIC_GATEWAY",      AP_V(sysConfig->static_gateway))
      TRY_PH("STATIC_SUBNET",       AP_V(sysConfig->static_subnet))
      TRY_PH("STATIC_DNS",          AP_V(sysConfig->static_dns))
      TRY_PH("NTP_SERVER",          AP_V(sysConfig->ntp_server))
      // --- BMS 配置 ---
      TRY_PH("CELL_COUNT",          AP_N(bmsConfig->cell_count))
      TRY_PH("CAPACITY",            AP_N(bmsConfig->nominal_capacity_mAh))
      TRY_PH("BMS_CHARGE_CURRENT",  AP_N(bmsConfig->max_charge_current))
      TRY_PH("PWR_CHARGE_CURRENT",  AP_N(powerConfig->max_charge_current))
      TRY_PH("PWR_DISCHARGE_CURRENT", AP_N(powerConfig->max_discharge_current))
      TRY_PH("BMS_CHEM_NCM",        AP_S(bmsConfig->chemistry == CHEM_NCM ? " selected" : ""))
      TRY_PH("BMS_CHEM_LFP",        AP_S(bmsConfig->chemistry == CHEM_LFP ? " selected" : ""))
      TRY_PH("BMS_CELL_COUNT_3",    AP_S(bmsConfig->cell_count == 3 ? " selected" : ""))
      TRY_PH("BMS_CELL_COUNT_4",    AP_S(bmsConfig->cell_count == 4 ? " selected" : ""))
      TRY_PH("BMS_CELL_COUNT_5",    AP_S(bmsConfig->cell_count == 5 ? " selected" : ""))
      TRY_PH("BMS_NOMINAL_CAPACITY", AP_N(bmsConfig->nominal_capacity_mAh))
      TRY_PH("BMS_CELL_OV",         AP_N(bmsConfig->cell_ov_threshold))
      TRY_PH("BMS_CELL_UV",         AP_N(bmsConfig->cell_uv_threshold))
      TRY_PH("BMS_CELL_OV_RECOVER", AP_N(bmsConfig->cell_ov_recover))
      TRY_PH("BMS_CELL_UV_RECOVER", AP_N(bmsConfig->cell_uv_recover))
      TRY_PH("BMS_MAX_CHARGE",      AP_N(bmsConfig->max_charge_current))
      TRY_PH("BMS_MAX_DISCHARGE",   AP_N(bmsConfig->max_discharge_current))
      TRY_PH("BMS_SHORT_CIRCUIT",   AP_N(bmsConfig->short_circuit_threshold))
      TRY_PH("BMS_OVERHEAT_THRESHOLD", AP_F(bmsConfig->temp_overheat_threshold))
      TRY_PH("BMS_BALANCING_CHECKED", AP_S(bmsConfig->balancing_enabled ? "checked" : ""))
      TRY_PH("BMS_BALANCING_DIFF",  AP_F(bmsConfig->balancing_voltage_diff))
      // --- Power 配置 ---
      TRY_PH("POWER_MAX_CHARGE",    AP_N(powerConfig->max_charge_current))
      TRY_PH("POWER_CHARGE_VOLTAGE", AP_N(powerConfig->charge_voltage_limit))
      TRY_PH("POWER_CHARGE_SOC_START", do { snprintf(tmp, sizeof(tmp), "%.0f", powerConfig->charge_soc_start); AP_S(tmp); } while(0))
      TRY_PH("POWER_CHARGE_SOC_STOP", do { snprintf(tmp, sizeof(tmp), "%.0f", powerConfig->charge_soc_stop); AP_S(tmp); } while(0))
      TRY_PH("POWER_MAX_DISCHARGE", AP_N(powerConfig->max_discharge_current))
      TRY_PH("POWER_DISCHARGE_SOC_STOP", do { snprintf(tmp, sizeof(tmp), "%.0f", powerConfig->discharge_soc_stop); AP_S(tmp); } while(0))
      TRY_PH("POWER_HYBRID_CHECKED", AP_S(powerConfig->enable_hybrid_boost ? "checked" : ""))
      TRY_PH("POWER_VSYS_MIN",      AP_N(powerConfig->vsys_min_mV))
      TRY_PH("POWER_OVER_CURRENT",  AP_N(powerConfig->over_current_threshold))
      TRY_PH("POWER_OVER_TEMP",     AP_F(powerConfig->over_temp_threshold))
      TRY_PH("POWER_CHARGE_TEMP_HIGH", AP_F(powerConfig->charge_temp_high_limit))
      TRY_PH("POWER_CHARGE_TEMP_LOW", AP_F(powerConfig->charge_temp_low_limit))
      // --- OTA ---
      TRY_PH("FIRMWARE_VERSION",
        if (!isConfigMode && statePtr != nullptr) AP_V(statePtr->system.firmware_version);
        else AP_S("Config Mode");
      )
      TRY_PH("FREE_SKETCH_SPACE",   AP_L(ESP.getFreeSketchSpace() / 1024))
      TRY_PH("FLASH_SIZE",          AP_L(ESP.getFlashChipSize() / (1024 * 1024)))

      #undef TRY_PH

      // 未知占位符：原样输出 %
      out[j++] = *pos++;
      continue;
    }

    // 普通字符
    out[j++] = *pos++;
  }

  out[j] = '\0';

  #undef AP_S
  #undef AP_V
  #undef AP_N
  #undef AP_F
  #undef AP_L

  AsyncWebServerResponse* resp = request->beginResponse(200, "text/html", out);
  resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  request->send(resp);

  delete[] cssBuf;
  delete[] jsBuf;
  delete[] injectBuf;
  delete[] tpl;
  delete[] out;
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
  } else if (chemistry_change_pending_) {
    // 化学类型变更：重置电池学习数据（SOH/循环/库仑计基准全部失效）并重启，
    // 重启后 setup() 按新 chemistry 实例化正确子类
    chemistry_change_pending_ = false;
    DBG.println(F("[WebServer] Chemistry changed - resetting battery data & rebooting"));
    EventBus::getInstance().publish(EVT_BMS_RESET_BATTERY_DATA, nullptr);  // 同步执行

    responseDoc["message"] = "Battery chemistry changed. Battery data reset. Device will reboot...";
    responseDoc["restart_required"] = true;
    char responseBuffer[512];
    serializeJson(responseDoc, responseBuffer);
    request->send(200, "application/json", responseBuffer);
    delay(100);
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
        // 启用 MQTT，需要填写 broker 和 port（先取指针判空再 strlen，防 JSON null 崩溃）
        if (sys.containsKey("mqtt_broker")) {
          const char* broker = sys["mqtt_broker"];
          if (broker && broker[0]) strlcpy(tempSysConfig.mqtt_broker, broker, sizeof(tempSysConfig.mqtt_broker));
        }
        if (sys.containsKey("mqtt_port")) {
          uint32_t port = sys["mqtt_port"];
          if (port > 0 && port <= 65535) tempSysConfig.mqtt_port = (uint16_t)port;
          else addError("system.mqtt_port", "must be 1-65535");
        }
        if (sys.containsKey("mqtt_username")) {
          const char* usr = sys["mqtt_username"];
          if (usr && usr[0]) strlcpy(tempSysConfig.mqtt_username, usr, sizeof(tempSysConfig.mqtt_username));
        }
        if (sys.containsKey("mqtt_password")) {
          const char* pwd = sys["mqtt_password"];
          if (pwd && pwd[0]) strlcpy(tempSysConfig.mqtt_password, pwd, sizeof(tempSysConfig.mqtt_password));
        }
        // 启用但 broker 为空 → 校验失败，避免前端勾选但未填地址导致配置前后不一致
        if (tempSysConfig.mqtt_broker[0] == '\0' || tempSysConfig.mqtt_port == 0) {
          addError("system.mqtt_broker", "MQTT broker and port required when enabled");
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
    if (sys.containsKey("static_ip")) {
      const char* ip = sys["static_ip"];
      if (ip && ip[0]) strlcpy(tempSysConfig.static_ip, ip, sizeof(tempSysConfig.static_ip));
    }
    if (sys.containsKey("static_gateway")) {
      const char* gw = sys["static_gateway"];
      if (gw && gw[0]) strlcpy(tempSysConfig.static_gateway, gw, sizeof(tempSysConfig.static_gateway));
    }
    if (sys.containsKey("static_subnet")) {
      const char* sn = sys["static_subnet"];
      if (sn && sn[0]) strlcpy(tempSysConfig.static_subnet, sn, sizeof(tempSysConfig.static_subnet));
    }
    if (sys.containsKey("static_dns")) {
      const char* dns = sys["static_dns"];
      if (dns && dns[0]) strlcpy(tempSysConfig.static_dns, dns, sizeof(tempSysConfig.static_dns));
    }
    if (sys.containsKey("ntp_server")) {
      const char* ntp = sys["ntp_server"];
      if (ntp && ntp[0]) strlcpy(tempSysConfig.ntp_server, ntp, sizeof(tempSysConfig.ntp_server));
    }
    // ======================================
    
  }

  // Update BMS Configuration
  chemistry_change_pending_ = false;  // 每次请求重置
  if (doc.containsKey("bms")) {
    JsonVariantConst bms = doc["bms"];

    if (bms.containsKey("cell_count")) {
      uint8_t val = bms["cell_count"];
      if (val >= 3 && val <= 5) tempBmsConfig.cell_count = val;
      else addError("bms.cell_count", "must be 3-5");
    }

    // ---- 化学类型（变更需 重置电池数据+重启，由 handleSaveConfig 执行） ----
    if (bms.containsKey("chemistry")) {
      const char* chem_str = bms["chemistry"];
      uint8_t chem_new = tempBmsConfig.chemistry;
      if (chem_str && strcmp(chem_str, "ncm") == 0) chem_new = CHEM_NCM;
      else if (chem_str && strcmp(chem_str, "lifepo4") == 0) chem_new = CHEM_LFP;
      else addError("bms.chemistry", "must be 'ncm' or 'lifepo4'");

      if (chem_new != tempBmsConfig.chemistry) {
        tempBmsConfig.chemistry = chem_new;
        chemistry_change_pending_ = true;
        // 请求未携带新阈值时自动填入目标化学推荐值（拒绝混搭旧阈值）
        const ChemistryLimits& NL = getChemistryLimits((BatteryChemistry_t)chem_new);
        if (!bms.containsKey("cell_ov_threshold")) tempBmsConfig.cell_ov_threshold = NL.recommended_ov_mV;
        if (!bms.containsKey("cell_uv_threshold")) tempBmsConfig.cell_uv_threshold = NL.recommended_uv_mV;
        if (!bms.containsKey("cell_ov_recover"))   tempBmsConfig.cell_ov_recover   = NL.recommended_ov_rec_mV;
        if (!bms.containsKey("cell_uv_recover"))   tempBmsConfig.cell_uv_recover   = NL.recommended_uv_rec_mV;
        if (!doc.containsKey("power") || !doc["power"].containsKey("charge_voltage_limit")) {
          tempPowerConfig.charge_voltage_limit =
              (uint16_t)(tempBmsConfig.cell_count * NL.recommended_charge_cell_mV);
        }
      }
    }
    // 后续 OV/UV 校验统一按目标化学的边界表
    const ChemistryLimits& CL = getChemistryLimits((BatteryChemistry_t)tempBmsConfig.chemistry);

    if (bms.containsKey("nominal_capacity_mAh")) {
      uint32_t val = bms["nominal_capacity_mAh"];
      if (val > 0 && val <= 50000) tempBmsConfig.nominal_capacity_mAh = val;
      else addError("bms.nominal_capacity_mAh", "must be 1-50000 mAh");
    }
    if (bms.containsKey("cell_ov_threshold")) {
      uint16_t val = bms["cell_ov_threshold"];
      if (val >= CL.ov_range_min_mV && val <= CL.ov_range_max_mV) tempBmsConfig.cell_ov_threshold = val;
      else addError("bms.cell_ov_threshold", "out of range for selected chemistry");
    }
    if (bms.containsKey("cell_uv_threshold")) {
      uint16_t val = bms["cell_uv_threshold"];
      if (val >= CL.uv_range_min_mV && val <= CL.uv_range_max_mV) tempBmsConfig.cell_uv_threshold = val;
      else addError("bms.cell_uv_threshold", "out of range for selected chemistry");
    }
    if (bms.containsKey("cell_ov_recover")) {
      uint16_t val = bms["cell_ov_recover"];
      if (val >= CL.ov_rec_min_mV && val <= CL.ov_rec_max_mV) tempBmsConfig.cell_ov_recover = val;
      else addError("bms.cell_ov_recover", "out of range for selected chemistry");
    }
    if (bms.containsKey("cell_uv_recover")) {
      uint16_t val = bms["cell_uv_recover"];
      if (val >= CL.uv_rec_min_mV && val <= CL.uv_rec_max_mV) tempBmsConfig.cell_uv_recover = val;
      else addError("bms.cell_uv_recover", "out of range for selected chemistry");
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
  
  // 交叉校验：充电电压 ≤ 串数×(OV-10mV)
  if (!configManager->validateCrossConfig(tempBmsConfig, tempPowerConfig, true)) {
    addError("power.charge_voltage_limit", "must be <= cell_count*(cell_ov_threshold-10mV)");
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
  // 化学切换自动派生了 charge_voltage_limit，即使请求未携带 power 段也必须提交，
  // 否则 NVS 中残留旧化学的充电电压（如 NCM 12450mV 灌 3S LFP），重启后过充
  if ((doc.containsKey("power") || chemistry_change_pending_) &&
      !configManager->updatePowerConfig(tempPowerConfig, false)) {
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

// =============================================================================
// Webhook API 处理函数
// =============================================================================

void WebServer::handleWebhookGet(AsyncWebServerRequest* request) {
  if (!webhookManager) {
    sendErrorResponse(request, I18n::get(STR_WH_NOT_INIT), 503);
    return;
  }

  WebhookConfig_t cfg = webhookManager->getConfig();
  DynamicJsonDocument doc(4096);

  doc["global_enabled"] = cfg.global_enabled;
  doc["endpoint_count"] = cfg.endpoint_count;

  JsonArray endpoints = doc.createNestedArray("endpoints");
  for (uint8_t i = 0; i < cfg.endpoint_count && i < WH_MAX_ENDPOINTS; i++) {
    const WebhookEndpoint_t& ep = cfg.endpoints[i];
    JsonObject obj = endpoints.createNestedObject();
    obj["enabled"] = ep.enabled;
    obj["verify_tls"] = ep.verify_tls;
    obj["method"] = ep.method;
    obj["name"] = ep.name;
    obj["url"] = ep.url;
    obj["auth_token"] = String(ep.auth_token).length() > 0 ? "***" : "";
    obj["device_key"] = String(ep.device_key).length() > 0 ? "***" : "";
    obj["auth_header"] = ep.auth_header;
    obj["cooldown_ms"] = ep.cooldown_ms;
    obj["message_template"] = ep.message_template;
    obj["trigger_count"] = ep.trigger_count;

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

    // 统计信息
    WebhookEndpointStats_t stats;
    if (webhookManager->getEndpointStats(i, stats)) {
      obj["total_sent"] = stats.total_sent;
      obj["total_failed"] = stats.total_failed;
      obj["total_resolved"] = stats.total_resolved;
      obj["last_success"] = stats.last_success;
    }
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServer::handleWebhookPost(AsyncWebServerRequest* request) {
  if (!webhookManager) {
    sendErrorResponse(request, I18n::get(STR_WH_NOT_INIT), 503);
    return;
  }

  String body;
  if (!takeBody(request, body)) {
    sendErrorResponse(request, I18n::get(STR_WH_MISSING_BODY), 400);
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendErrorResponse(request, I18n::get(STR_WH_BAD_JSON), 400);
    return;
  }

  // 以当前配置为基础合并，避免漏字段被清空
  WebhookConfig_t newConfig = webhookManager->getConfig();
  newConfig.config_version = WH_CONFIG_VERSION;

  // 全局开关
  if (doc.containsKey("global_enabled")) {
    newConfig.global_enabled = doc["global_enabled"].as<bool>();
  }

  // 端点数组
  if (doc.containsKey("endpoints")) {
    JsonArray arr = doc["endpoints"].as<JsonArray>();
    uint8_t count = 0;
    for (JsonObject ep : arr) {
      if (count >= WH_MAX_ENDPOINTS) break;

      // 超出旧端点数的位置初始化为默认值
      if (count >= newConfig.endpoint_count) {
        memset(&newConfig.endpoints[count], 0, sizeof(WebhookEndpoint_t));
        newConfig.endpoints[count].cooldown_ms = WH_DEFAULT_COOLDOWN;
        newConfig.endpoints[count].verify_tls = true;
      }
      WebhookEndpoint_t& dest = newConfig.endpoints[count];

      if (ep.containsKey("enabled")) dest.enabled = ep["enabled"].as<bool>();
      if (ep.containsKey("verify_tls")) dest.verify_tls = ep["verify_tls"].as<bool>();
      if (ep.containsKey("method")) dest.method = ep["method"].as<uint8_t>();
      if (ep.containsKey("name")) strlcpy(dest.name, ep["name"] | "", sizeof(dest.name));
      if (ep.containsKey("url")) strlcpy(dest.url, ep["url"] | "", sizeof(dest.url));
      if (ep.containsKey("cooldown_ms")) dest.cooldown_ms = ep["cooldown_ms"] | WH_DEFAULT_COOLDOWN;
      if (ep.containsKey("message_template")) strlcpy(dest.message_template, ep["message_template"] | "", sizeof(dest.message_template));
      if (ep.containsKey("auth_header")) strlcpy(dest.auth_header, ep["auth_header"] | "", sizeof(dest.auth_header));

      // Token 处理: "***" 保留已有；clear_token=true 清除；其他值更新
      if (ep.containsKey("auth_token")) {
        const char* token = ep["auth_token"] | "";
        bool clearToken = ep["clear_token"] | false;
        if (clearToken) {
          dest.auth_token[0] = '\0';
        } else if (strcmp(token, "***") != 0 && strlen(token) > 0) {
          strlcpy(dest.auth_token, token, sizeof(dest.auth_token));
        }
      }

      // Device Key 处理: "***" 保留已有；clear_key=true 清除；其他值更新
      if (ep.containsKey("device_key")) {
        const char* key = ep["device_key"] | "";
        bool clearKey = ep["clear_key"] | false;
        if (clearKey) {
          dest.device_key[0] = '\0';
        } else if (strcmp(key, "***") != 0 && strlen(key) > 0) {
          strlcpy(dest.device_key, key, sizeof(dest.device_key));
        }
      }

      // 触发器数组 (整体重建，避免残留旧触发器数据)
      if (ep.containsKey("triggers")) {
        JsonArray trigs = ep["triggers"].as<JsonArray>();
        uint8_t trigCount = 0;
        for (JsonObject t : trigs) {
          if (trigCount >= WH_MAX_TRIGGERS) break;
          WebhookTrigger_t& tDest = dest.triggers[trigCount];
          memset(&tDest, 0, sizeof(tDest));

          tDest.enabled = t["enabled"] | false;
          tDest.alert_level = t["alert_level"] | 0;
          tDest.condition.trigger_type = t["trigger_type"] | 0;
          tDest.condition.source = t["source"] | 0;
          tDest.condition.compare_op = t["compare_op"] | 0;
          tDest.condition.threshold = t["threshold"] | 0.0f;
          strlcpy(tDest.dedup_key, t["dedup_key"] | "", sizeof(tDest.dedup_key));
          strlcpy(tDest.title, t["title"] | "", sizeof(tDest.title));
          strlcpy(tDest.description, t["description"] | "", sizeof(tDest.description));
          tDest.fired = false;

          trigCount++;
        }
        dest.trigger_count = trigCount;
      }

      count++;
    }
    newConfig.endpoint_count = count;
  }

  char reason[64] = {0};
  if (!webhookManager->updateConfig(newConfig, reason)) {
    String msg = (reason[0] != '\0') ? String(I18n::get(STR_WH_VALIDATE_FAIL))
                                     : String(I18n::get(STR_WH_SAVE_FAIL));
    if (reason[0] != '\0') {
      msg += ": ";
      msg += reason;
    }
    sendErrorResponse(request, msg, 400);
    return;
  }

  String okMsg = String("{\"success\":true,\"message\":\"") + I18n::get(STR_WH_SAVED) + "\"}";
  request->send(200, "application/json", okMsg);
}

void WebServer::handleWebhookTest(AsyncWebServerRequest* request) {
  if (!webhookManager) {
    sendErrorResponse(request, I18n::get(STR_WH_NOT_INIT), 503);
    return;
  }

  String body;
  if (!takeBody(request, body)) {
    sendErrorResponse(request, I18n::get(STR_WH_MISSING_BODY), 400);
    return;
  }

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body)) {
    sendErrorResponse(request, I18n::get(STR_WH_BAD_JSON), 400);
    return;
  }

  uint8_t idx = doc["endpoint_index"] | 0;
  if (idx >= WH_MAX_ENDPOINTS) {
    sendErrorResponse(request, I18n::get(STR_WH_INVALID_INDEX), 400);
    return;
  }

  // 同步发送测试消息，返回真实 HTTP 结果（最多阻塞 WH_HTTP_TIMEOUT）
  String responseMsg;
  int httpCode = 0;
  bool success = webhookManager->sendTestNow(idx, responseMsg, httpCode);

  DynamicJsonDocument resp(512);
  resp["success"] = success;
  resp["queued"] = false;
  resp["http_code"] = httpCode;
  resp["message"] = responseMsg;
  String output;
  serializeJson(resp, output);
  request->send(200, "application/json", output);
}