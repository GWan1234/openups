#include "debug.h"
#include <time.h>

#if !DEBUG_MODE
NullSerial_ NullSerial;
#endif

#if DEBUG_MODE

// =============================================================================
// DebugLogger 实现
// =============================================================================

DebugLogger& DebugLogger::getInstance() {
    static DebugLogger instance;
    return instance;
}

DebugLogger::DebugLogger()
    : m_mode(DEFAULT_LOG_MODE)
    , m_fs(nullptr)
    , m_initialized(false)
    , m_serial_begun(false)
    , m_bufferCount(0)
    , m_linePos(0)
    , m_lastFlushTime(0)
    , m_currentFileSize(0)
    , m_currentFileIndex(0)
    , m_lastCleanupTime(0)
    , m_mutex(nullptr)
{
    m_currentFilePath[0] = '\0';
    m_lineBuf[0] = '\0';
    memset(m_bufferLens, 0, sizeof(m_bufferLens));
}

DebugLogger::~DebugLogger() {
    if (m_initialized) {
        flushBuffer();
    }
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

bool DebugLogger::begin(fs::FS& fs, LogOutputMode mode) {
    if (m_initialized) return true;  // 防止重复初始化

    m_fs = &fs;
    m_mode = mode;
    m_initialized = true;
    m_lastFlushTime = millis();

    // 创建互斥锁
    if (!m_mutex) {
        m_mutex = xSemaphoreCreateMutex();
    }

    // 如果需要串口输出，确保串口已初始化
    if (m_mode == LogOutputMode::LOG_SERIAL_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        if (!m_serial_begun) {
            Serial.begin(115200);
            m_serial_begun = true;
        }
    }

    // 如果需要文件输出，创建日志目录并清理旧日志
    if (m_mode == LogOutputMode::LOG_FILE_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        // 确保 /log/ 目录存在
        if (!m_fs->exists("/log")) {
            m_fs->mkdir("/log");
        }
        // 启动时清理一次过期日志
        cleanupOldLogs();
    }

    return true;
}

size_t DebugLogger::write(uint8_t c) {
    if (m_mode == LogOutputMode::LOG_DISABLED) return 1;

    // 串口输出
    if (m_mode == LogOutputMode::LOG_SERIAL_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        Serial.write(c);
    }

    // 文件输出：积累到行缓冲区
    if (m_mode == LogOutputMode::LOG_FILE_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        char lineBuf[LOG_MAX_LINE_LEN];
        size_t lineLen = 0;
        bool hasLine = false;

        // 在锁内只做行缓冲区操作
        if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (c == '\n' || c == '\r') {
                if (m_linePos > 0) {
                    memcpy(lineBuf, m_lineBuf, m_linePos);
                    lineBuf[m_linePos] = '\0';
                    lineLen = m_linePos;
                    m_linePos = 0;
                    hasLine = true;
                }
            } else if (m_linePos < LOG_MAX_LINE_LEN - 2) {
                m_lineBuf[m_linePos++] = (char)c;
            }
            xSemaphoreGive(m_mutex);
        }

        // 在锁外调用 writeLineToBuffer，避免死锁
        if (hasLine) {
            writeLineToBuffer(lineBuf, lineLen);
        }
    }

    return 1;
}

size_t DebugLogger::write(const uint8_t* buf, size_t size) {
    if (m_mode == LogOutputMode::LOG_DISABLED) return size;

    // 串口输出
    if (m_mode == LogOutputMode::LOG_SERIAL_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        Serial.write(buf, size);
    }

    // 文件输出：逐字符处理（寻找换行符）
    if (m_mode == LogOutputMode::LOG_FILE_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        for (size_t i = 0; i < size; i++) {
            uint8_t c = buf[i];
            char lineBuf[LOG_MAX_LINE_LEN];
            size_t lineLen = 0;
            bool hasLine = false;

            // 在锁内只做行缓冲区操作
            if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (c == '\n') {
                    if (m_linePos > 0) {
                        memcpy(lineBuf, m_lineBuf, m_linePos);
                        lineBuf[m_linePos] = '\0';
                        lineLen = m_linePos;
                        m_linePos = 0;
                        hasLine = true;
                    }
                } else if (c != '\r' && m_linePos < LOG_MAX_LINE_LEN - 2) {
                    m_lineBuf[m_linePos++] = (char)c;
                }
                xSemaphoreGive(m_mutex);
            }

            // 在锁外调用 writeLineToBuffer，避免死锁
            if (hasLine) {
                writeLineToBuffer(lineBuf, lineLen);
            }
        }
    }

    return size;
}

int DebugLogger::availableForWrite() {
    if (m_mode == LogOutputMode::LOG_SERIAL_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        return Serial.availableForWrite();
    }
    return 1024; // 返回一个假值
}

void DebugLogger::flush() {
    if (m_mode == LogOutputMode::LOG_SERIAL_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        Serial.flush();
    }
    // 文件输出的flush由定时器控制
}

void DebugLogger::setMode(LogOutputMode mode) {
    if (m_mode == mode) return;

    // 切换前先flush缓冲区
    flushBuffer();

    m_mode = mode;

    // 如果切换到需要串口的模式，确保串口已初始化
    if (m_mode == LogOutputMode::LOG_SERIAL_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        if (!m_serial_begun) {
            Serial.begin(115200);
            m_serial_begun = true;
        }
    }

    // 如果切换到需要文件的模式，确保目录存在
    if (m_mode == LogOutputMode::LOG_FILE_ONLY || m_mode == LogOutputMode::LOG_BOTH) {
        if (m_fs && !m_fs->exists("/log")) {
            m_fs->mkdir("/log");
        }
    }
}

void DebugLogger::writeLineToBuffer(const char* line, size_t len) {
    if (!m_initialized || !m_fs) return;

    // 构建带时间戳的日志行
    char timestamped[LOG_MAX_LINE_LEN];
    size_t tsLen = buildTimestamp(timestamped, sizeof(timestamped));
    size_t remaining = LOG_MAX_LINE_LEN - tsLen - 2; // 留出换行符空间
    size_t copyLen = (len < remaining) ? len : remaining;
    memcpy(timestamped + tsLen, line, copyLen);
    timestamped[tsLen + copyLen] = '\0';
    size_t totalLen = tsLen + copyLen;

    // 存入环形缓冲区
    appendToBuffer(timestamped, totalLen);

    // 检查是否需要flush
    checkFlushCondition();
}

void DebugLogger::appendToBuffer(const char* data, size_t len) {
    if (m_bufferCount >= LOG_BUFFER_SIZE) {
        // 缓冲区已满，先flush
        flushBuffer();
    }

    size_t copyLen = (len < LOG_MAX_LINE_LEN - 1) ? len : LOG_MAX_LINE_LEN - 1;
    memcpy(m_buffer[m_bufferCount], data, copyLen);
    m_buffer[m_bufferCount][copyLen] = '\0';
    m_bufferLens[m_bufferCount] = copyLen;
    m_bufferCount++;
}

void DebugLogger::checkFlushCondition() {
    unsigned long now = millis();

    // 条件1：缓冲区满
    if (m_bufferCount >= LOG_BUFFER_SIZE) {
        flushBuffer();
        return;
    }

    // 条件2：定时器触发
    if (now - m_lastFlushTime >= LOG_FLUSH_INTERVAL) {
        flushBuffer();
        return;
    }
}

void DebugLogger::flushBuffer() {
    if (m_bufferCount == 0) return;
    if (!m_initialized || !m_fs) return;

    // 非文件模式，只清空缓冲区
    if (m_mode != LogOutputMode::LOG_FILE_ONLY && m_mode != LogOutputMode::LOG_BOTH) {
        if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            m_bufferCount = 0;
            m_lastFlushTime = millis();
            xSemaphoreGive(m_mutex);
        }
        return;
    }

    // 文件模式需要更长的锁持有时间
    if (!m_mutex || xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return;
    }

    // 获取当前日期
    char dateStr[12];
    getDateString(dateStr, sizeof(dateStr));

    // 检查日期是否变化（只比较日期部分）
    char* dateInPath = strstr(m_currentFilePath, "/log/");
    if (!dateInPath || strncmp(dateInPath + 5, dateStr, 10) != 0) {
        // 日期变化，重置文件
        m_currentFilePath[0] = '\0';
        m_currentFileSize = 0;
        m_currentFileIndex = 0;
    }

    // 打开或创建日志文件
    openLogFile(dateStr);

    // 写入缓冲区内容
    File file = m_fs->open(m_currentFilePath, FILE_APPEND);
    if (file) {
        for (size_t i = 0; i < m_bufferCount; i++) {
            file.write((const uint8_t*)m_buffer[i], m_bufferLens[i]);
            file.write((const uint8_t*)"\n", 1);
            m_currentFileSize += m_bufferLens[i] + 1;
        }
        file.close();
    }

    m_bufferCount = 0;
    m_lastFlushTime = millis();

    // 释放互斥锁
    xSemaphoreGive(m_mutex);

    // 检查是否需要清理旧日志（每天一次）
    unsigned long now = millis();
    if (now - m_lastCleanupTime >= 86400000UL) {
        cleanupOldLogs();
        m_lastCleanupTime = now;
    }
}

void DebugLogger::openLogFile(const char* date) {
    // 如果当前文件已存在且未超过大小限制，继续使用
    if (m_currentFilePath[0] != '\0' && m_currentFileSize < LOG_MAX_FILE_SIZE) {
        return;
    }

    // 生成文件路径
    if (m_currentFileIndex == 0) {
        snprintf(m_currentFilePath, sizeof(m_currentFilePath), "/log/%s.log", date);
    } else {
        snprintf(m_currentFilePath, sizeof(m_currentFilePath), "/log/%s.%d.log", date, m_currentFileIndex);
    }

    // 检查文件是否存在，如果存在且超过大小限制，增加索引
    if (m_fs->exists(m_currentFilePath)) {
        File f = m_fs->open(m_currentFilePath, "r");
        if (f) {
            m_currentFileSize = f.size();
            f.close();
            if (m_currentFileSize >= LOG_MAX_FILE_SIZE) {
                m_currentFileIndex++;
                snprintf(m_currentFilePath, sizeof(m_currentFilePath), "/log/%s.%d.log", date, m_currentFileIndex);
                m_currentFileSize = 0;
            }
        }
    } else {
        m_currentFileSize = 0;
    }
}

void DebugLogger::getDateString(char* buf, size_t size) {
    if (isTimeSynced()) {
        struct tm timeinfo;
        time_t now;
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(buf, size, "%Y-%m-%d", &timeinfo);
    } else {
        // NTP未同步时，使用开机天数作为日期
        unsigned long days = millis() / 86400000UL;
        snprintf(buf, size, "boot-%04lu", days);
    }
}

bool DebugLogger::isTimeSynced() {
    time_t now;
    time(&now);
    // 2026-01-01 之后认为时间已同步
    return (now > 1767225600LL);
}

size_t DebugLogger::buildTimestamp(char* buf, size_t size) {
    if (isTimeSynced()) {
        struct tm timeinfo;
        time_t now;
        time(&now);
        localtime_r(&now, &timeinfo);
        return strftime(buf, size, "[%Y-%m-%d %H:%M:%S] ", &timeinfo);
    } else {
        // 未同步时使用millis
        unsigned long ms = millis();
        unsigned long sec = ms / 1000;
        unsigned long min = sec / 60;
        unsigned long hr = min / 60;
        return snprintf(buf, size, "[%02lu:%02lu:%02lu.%03lu] ",
                        hr % 24, min % 60, sec % 60, ms % 1000);
    }
}

void DebugLogger::cleanupOldLogs() {
    if (!m_fs) return;

    // NTP未同步时跳过清理（无法判断文件是否过期）
    if (!isTimeSynced()) return;

    // 计算截止时间戳
    time_t now;
    time(&now);
    time_t cutoff = now - (LOG_MAX_RETENTION * 86400LL);

    // 遍历 /log/ 目录
    File root = m_fs->open("/log");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    File file = root.openNextFile();
    while (file) {
        String path = file.path();
        String name = file.name();
        file.close();  // 立即关闭，避免资源泄漏

        // 解析文件名中的日期 (格式: YYYY-MM-DD.log 或 YYYY-MM-DD.N.log)
        int dash1 = name.indexOf('-');
        int dash2 = name.indexOf('-', dash1 + 1);
        if (dash1 > 0 && dash2 > 0) {
            int dot = name.indexOf('.', dash2);
            if (dot > 0) {
                String dateStr = name.substring(0, dot);
                int year = dateStr.substring(0, dash1).toInt();
                int month = dateStr.substring(dash1 + 1, dash2).toInt();
                int day = dateStr.substring(dash2 + 1).toInt();

                if (year >= 2020 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
                    struct tm tm_file = {};
                    tm_file.tm_year = year - 1900;
                    tm_file.tm_mon = month - 1;
                    tm_file.tm_mday = day;
                    time_t file_time = mktime(&tm_file);

                    if (file_time < cutoff) {
                        m_fs->remove(path);
                    }
                }
            }
        }

        file = root.openNextFile();
    }
    root.close();
}

#endif // DEBUG_MODE
