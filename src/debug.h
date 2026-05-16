#ifndef DEBUG_H
#define DEBUG_H

// =============================================================================
// 调试模式开关
// 设为 1 启用 Serial 输出 (调试/开发模式)
// 设为 0 禁用 Serial 输出 (生产模式，降低功耗)
// =============================================================================
#ifndef DEBUG_MODE
#define DEBUG_MODE  0
#endif

#if DEBUG_MODE

#define DBG Serial

#else

#include <Stream.h>

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
