#ifndef I2C_INTERFACE_H
#define I2C_INTERFACE_H

#include <Arduino.h>
#include <Wire.h>

class I2CInterface {
public:
    I2CInterface();
    
    // 初始化和管理
    bool initialize(int sdaPin, int sclPin, uint32_t frequency = 100000);
    bool isDeviceConnected(uint8_t deviceAddr);
    void scanI2CBus();
    
    // 基本读写操作
    bool readRegisterByte(uint8_t deviceAddr, uint8_t reg, uint8_t *value);
    bool writeRegisterByte(uint8_t deviceAddr, uint8_t reg, uint8_t value);
    bool readRegisterWord(uint8_t deviceAddr, uint8_t reg, uint16_t *value);
    bool writeRegisterWord(uint8_t deviceAddr, uint8_t reg, uint16_t value);
    
    // CRC校验读写操作
    bool readRegisterByteCRC(uint8_t deviceAddr, uint8_t reg, uint8_t *value);
    bool readRegisterWordCRC(uint8_t deviceAddr, uint8_t reg, uint16_t *value);
    bool readRegistersWordCRC(uint8_t deviceAddr, uint8_t startReg, uint16_t *values, uint8_t count);
    bool writeRegisterByteCRC(uint8_t deviceAddr, uint8_t reg, uint8_t value);

    // 原始I2C操作（无寄存器地址，适用于命令式协议如SHTC3）
    bool writeCommand(uint8_t deviceAddr, const uint8_t* data, uint8_t len);
    bool readRaw(uint8_t deviceAddr, uint8_t* data, uint8_t len);

private:
    TwoWire* i2cBus;
    bool initialized;
    
    bool checkInitialized();
    static uint8_t CRC8(const uint8_t *ptr, uint8_t len);
};

#endif
