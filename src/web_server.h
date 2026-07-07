#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "data_structures.h"
#include "config_manager.h"

// Forward declaration
class SystemManagement;
 
class WebServer {
private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    ConfigManager* configManager;
    SystemManagement* systemManager;  // 添加SystemManagement指针（nullptr = 配置模式）

    // 辅助函数声明
    void setupHttpRoutes();
    bool updateConfigurationFromRequest(const JsonDocument& doc, JsonDocument& errorDoc);
    void sendErrorResponse(AsyncWebServerRequest *request, const String& message, int code);
    void sendConfigModeResponse(AsyncWebServerRequest *request);

    // 路由注册辅助：统一认证门卫与请求体收集，消除每条路由的重复样板
    void onAuth(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler);
    void onAuthBody(const char* uri, ArRequestHandlerFunction handler);   // POST + body + 认证
    void onBody(const char* uri, ArRequestHandlerFunction handler);       // POST + body，无认证（登录/首次设置）

    // ========== 认证：登录页 + Session Token (Cookie) ==========
    // 已设置凭证时：优先校验 UPSSESSION Cookie，其次接受 HTTP Basic Auth
    // （方便 curl/脚本调用）；未设置凭证时：配置模式放行（初始向导需可访问），
    // 正常模式强制先设置账户密码
    static const uint8_t MAX_SESSIONS = 4;                             // 并发会话上限，超出时淘汰最久未活动的
    static const unsigned long SESSION_TTL_MS = 24UL * 3600UL * 1000UL; // 24h 滑动过期
    struct AuthSession {
        char token[33];           // 32 hex + '\0'
        unsigned long last_seen;  // millis()，滑动续期
        bool valid;
    };
    AuthSession sessions_[MAX_SESSIONS];

    // 登录暴力破解限制：连续失败 5 次锁定 60 秒
    uint8_t login_failures_;
    unsigned long login_lockout_until_;

    bool isAuthenticated(AsyncWebServerRequest* request);
    bool ensureAuthenticated(AsyncWebServerRequest* request);
    const char* createSession();                                  // 返回新 token（指向内部存储）
    bool checkSessionCookie(AsyncWebServerRequest* request);      // 校验并滑动续期
    void destroySessionFromRequest(AsyncWebServerRequest* request);
    void invalidateAllSessions();
    // 解析请求体中的 username/password；失败时已发送错误响应并返回 false
    bool parseCredentials(AsyncWebServerRequest* request, String& username, String& password);
    // 发送 JSON 响应并附带新建会话的 Set-Cookie（登录成功 / 首次设置成功）
    void sendJsonWithNewSession(AsyncWebServerRequest* request, const char* json);
    void handleAuthLogin(AsyncWebServerRequest* request);
    void handleAuthLogout(AsyncWebServerRequest* request);
    void handleAuthSetup(AsyncWebServerRequest* request);
    void handleAuthChange(AsyncWebServerRequest* request);

    // OTA 上传完成后的结果响应（成功则调度重启）
    void handleFirmwareResult(AsyncWebServerRequest* request);
    // 语言切换接口
    void handleSetLang(AsyncWebServerRequest* request);

    // SPA 页面渲染
    void renderSPA(AsyncWebServerRequest *request);
    
public:
    WebServer(ConfigManager* configMgr, SystemManagement* sysMgr, int port = 80);
    ~WebServer();
    bool begin();
    void notifyClients();
    
    // 页面处理函数
    void handleRoot(AsyncWebServerRequest *request);
    void handleSaveConfig(AsyncWebServerRequest *request);
    
    // API 处理函数
    void handleApiRequest(AsyncWebServerRequest* request, const char* route);
    void buildStatusResponse(DynamicJsonDocument& doc, const System_Global_State& state);
    void buildBmsResponse(DynamicJsonDocument& doc, const System_Global_State& state);
    void buildPowerResponse(DynamicJsonDocument& doc, const System_Global_State& state);
    
    // Prometheus metrics
    void handleMetricsRequest(AsyncWebServerRequest* request);

    // OTA update handlers
    void handleFirmwareUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);

    // BMS Ship Mode handler
    void handleBmsShipMode(AsyncWebServerRequest* request);

    // BMS Reset Battery Data handler
    void handleBmsResetData(AsyncWebServerRequest* request);

    // Clear tips handler
    void handleClearTips(AsyncWebServerRequest* request);

    // Restart device handler
    void handleRestart(AsyncWebServerRequest* request);

    // Raw data file handlers
    void handleRawFileList(AsyncWebServerRequest* request);
    void handleRawFileDownload(AsyncWebServerRequest* request);

    // Log file handlers
    void handleLogFileList(AsyncWebServerRequest* request);
    void handleLogFileDownload(AsyncWebServerRequest* request);

    // Generic file handlers
    void handleFileList(AsyncWebServerRequest* request, const char* dirPath);
    void handleFileDownload(AsyncWebServerRequest* request, const char* allowedDir);

    // ADC Calibration API handlers
    void handleCalibrationGet(AsyncWebServerRequest* request);
    void handleCalibrationPost(AsyncWebServerRequest* request);

    // 辅助函数
    void replaceStringInBuffer(char* buffer, size_t bufferSize, const char* search, const char* replace, char* tempBuffer);

    // WebSocket 事件处理
    void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len);
};

#endif