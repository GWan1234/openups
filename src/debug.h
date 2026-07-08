#ifndef DEBUG_H
#define DEBUG_H

#include <Stream.h>

// =============================================================================
// 调试模式开关
// 设为 1 启用调试输出 (调试/开发模式)
// 设为 0 禁用调试输出 (生产模式，降低功耗)
// =============================================================================
#ifndef DEBUG_MODE
#define DEBUG_MODE  0
#endif

// =============================================================================
// 日志输出模式配置
// LOG_DISABLED     - 关闭所有输出（零开销）
// LOG_SERIAL_ONLY  - 仅串口输出
// LOG_FILE_ONLY    - 仅文件输出（写入SPIFFS /log/ 目录）
// LOG_BOTH         - 串口+文件都输出
// =============================================================================
enum class LogOutputMode : uint8_t {
    LOG_DISABLED = 0,
    LOG_SERIAL_ONLY,
    LOG_FILE_ONLY,
    LOG_BOTH
};

// 默认日志模式（用户可修改此处切换输出方式）
#ifndef DEFAULT_LOG_MODE
#define DEFAULT_LOG_MODE LogOutputMode::LOG_FILE_ONLY
#endif

// =============================================================================
// 日志缓冲区和定时参数
// =============================================================================
#define LOG_BUFFER_SIZE     20          // 缓冲区条数（积累多少条后写入文件）
#define LOG_FLUSH_INTERVAL  60000UL     // flush间隔（毫秒）
#define LOG_MAX_RETENTION   7          // 最大保留天数
#define LOG_MAX_FILE_SIZE   (256*1024)  // 单个日志文件最大256KB
#define LOG_MAX_LINE_LEN    256         // 单条日志最大字节数

// =============================================================================
// DebugLogger 类
// =============================================================================
#if DEBUG_MODE

#include <Arduino.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class DebugLogger : public Stream {
public:
    static DebugLogger& getInstance();

    // 初始化（需在setup中调用，传入SPIFFS引用）
    bool begin(fs::FS& fs, LogOutputMode mode = DEFAULT_LOG_MODE);

    // Stream接口实现（兼容DBG宏的所有调用方式）
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t size) override;
    int availableForWrite() override;
    void flush() override;

    // 配置方法
    void setMode(LogOutputMode mode);
    LogOutputMode getMode() const { return m_mode; }

    // 手动flush和清理
    void flushBuffer();
    void cleanupOldLogs();

    // 原有Serial兼容方法（DBG.begin(115200) 调用此方法）
    void begin(unsigned long baud = 115200, uint32_t config = 0x800001c) {
        Serial.begin(baud, config);
        m_serial_begun = true;
        // 确保互斥锁已创建，避免后续write()崩溃
        if (!m_mutex) {
            m_mutex = xSemaphoreCreateMutex();
        }
        // 强制切换到串口模式，确保启动期间日志可输出
        // 正式模式在 begin(fs::FS&, LogOutputMode) 中设置
        m_mode = LogOutputMode::LOG_SERIAL_ONLY;
    }
    void end() { Serial.end(); }
    int available() override { return Serial.available(); }
    int read() override { return Serial.read(); }
    int peek() override { return Serial.peek(); }
    operator bool() const { return true; }

private:
    DebugLogger();
    ~DebugLogger();

    LogOutputMode m_mode;
    fs::FS* m_fs;
    bool m_initialized;
    bool m_serial_begun;

    // 缓冲区
    char m_buffer[LOG_BUFFER_SIZE][LOG_MAX_LINE_LEN];
    size_t m_bufferLens[LOG_BUFFER_SIZE];
    size_t m_bufferCount;
    size_t m_linePos;                       // 当前行写入位置
    char m_lineBuf[LOG_MAX_LINE_LEN];       // 当前行缓冲

    // 定时控制
    unsigned long m_lastFlushTime;

    // 当前文件信息
    char m_currentFilePath[32];             // "/log/YYYY-MM-DD.log" 或 "/log/YYYY-MM-DD.N.log"
    size_t m_currentFileSize;
    uint8_t m_currentFileIndex;

    // 清理控制
    unsigned long m_lastCleanupTime;

    // 线程安全
    SemaphoreHandle_t m_mutex;

    // 内部方法
    void appendToBuffer(const char* data, size_t len);
    void writeLineToBuffer(const char* line, size_t len);
    void checkFlushCondition();
    void openLogFile(const char* date);
    void getDateString(char* buf, size_t size);
    bool isTimeSynced();
    size_t buildTimestamp(char* buf, size_t size);
};

#define DBG DebugLogger::getInstance()

#else

// 生产模式（DEBUG_MODE=0）：使用NullSerial，零开销
class NullSerial_ : public Stream {
public:
    NullSerial_() {}
    void begin(unsigned long baud = 115200, uint32_t config = 0x800001c) {}
    void end() {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *buffer, size_t size) override { return size; }
    int availableForWrite() override { return 0; }
    operator bool() const { return false; }
};

extern NullSerial_ NullSerial;
#define DBG NullSerial

#endif // DEBUG_MODE

#endif // DEBUG_H
