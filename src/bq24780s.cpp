#include "bq24780s.h"
#include "i2c_interface.h"
#include "hardware_interface.h"
#include <Arduino.h>
#include "debug.h"

// =============================================================================
// Constructors
// =============================================================================


BQ24780S::BQ24780S(I2CInterface* i2c) 
    : i2c(i2c)
    , initialized_(false)
    , current_iadp_gain_(BQ24780SConst::IadpGain::Gain20x)
    , current_idchg_gain_(BQ24780SConst::IdchgGain::Gain16x) {
}


// =============================================================================
// Public Methods
// =============================================================================

bool BQ24780S::begin() {
    if (i2c == nullptr) {
        DBG.println(F("BQ24780S: I2C interface not available"));
        return false;
    }

    // 检查设备是否连接
    if (!isConnected()) {
        DBG.println(F("BQ24780S: Device not connected"));
        return false;
    }

    initialized_ = true;

    // 读取 DeviceID 检测芯片型号
    uint16_t device_id = 0;
    if (readRegister(0xFF, &device_id)) {
        if (device_id == 0x38) {
            chip_variant_ = BQ24780SConst::ChipVariant::BQ24800;
            DBG.println(F("BQ24780S: Detected BQ24800 (DeviceID=0x38)"));
        } else {
            chip_variant_ = BQ24780SConst::ChipVariant::BQ24780S;
            DBG.printf_P(PSTR("BQ24780S: Detected BQ24780S (DeviceID=0x%02X)\n"), device_id);
        }
    } else {
        DBG.println(F("BQ24780S: Failed to read DeviceID, assuming BQ24780S"));
    }

    //清除充电设置
    setChargeCurrent(0);
    setChargeVoltage(0);
    DBG.println(F("BQ24780S: Initialized successfully"));
    return true;
}

bool BQ24780S::applyHardwareConfig(const HardwareConfig& config) {
    if (!initialized_) {
        DBG.println(F("BQ24780S: Not initialized, cannot apply config"));
        return false;
    }
    
    DBG.println(F("BQ24780S: Applying hardware configuration..."));
    bool success = true;
    
    // 1. 设置增益配置
    if (setIadpGain(config.iadp_gain)) {
        DBG.printf_P(PSTR("BQ24780S: IADP gain set to %s\n"), 
                     config.iadp_gain == BQ24780SConst::IadpGain::Gain20x ? "20x" : "40x");
    } else {
        DBG.println(F("BQ24780S: Failed to set IADP gain"));
        success = false;
    }
    
    if (setIdchgGain(config.idchg_gain)) {
        DBG.printf_P(PSTR("BQ24780S: IDCHG gain set to %s\n"), 
                     config.idchg_gain == BQ24780SConst::IdchgGain::Gain8x ? "8x" : "16x");
    } else {
        DBG.println(F("BQ24780S: Failed to set IDCHG gain"));
        success = false;
    }
    
    // 2. 配置监测输出使能
    if (configureMonitoringOutputs(config.enable_idchg_monitor, config.enable_pmon_monitor)) {
        DBG.printf_P(PSTR("BQ24780S: Monitoring outputs configured (IDCHG=%s, PMON=%s)\n"),
                      config.enable_idchg_monitor ? "EN" : "DIS",
                      config.enable_pmon_monitor ? "EN" : "DIS");
    } else {
        DBG.println(F("BQ24780S: Failed to configure monitoring outputs"));
        success = false;
    }
    
    // 3. 配置放电调节
    if (configureDischargeRegulation(config.enable_discharge_regulation)) {
        DBG.printf_P(PSTR("BQ24780S: Discharge regulation %s\n"),
                      config.enable_discharge_regulation ? "enabled" : "disabled");
    } else {
        DBG.println(F("BQ24780S: Failed to configure discharge regulation"));
        success = false;
    }
    
    // 4. 配置混合供电增强模式
    if (setHybridPowerBoost(config.enable_hybrid_boost)) {
        DBG.printf_P(PSTR("BQ24780S: Hybrid power boost %s\n"),
                      config.enable_hybrid_boost ? "enabled" : "disabled");
    } else {
        DBG.println(F("BQ24780S: Failed to configure hybrid power boost"));
        success = false;
    }
    
    // 5. 配置电流限制（直接写入硬件，确保立即生效）
    if (setInputCurrentLimit(config.input_current_limit)) {
        DBG.printf_P(PSTR("BQ24780S: Input current limit set to %u mA\n"), config.input_current_limit);
    } else {
        DBG.println(F("BQ24780S: Failed to set input current limit"));
        success = false;
    }
    
    if (setDischargeCurrentLimit(config.discharge_current_max)) {
        DBG.printf_P(PSTR("BQ24780S: Discharge current limit set to %u mA\n"), config.discharge_current_max);
    } else {
        DBG.println(F("BQ24780S: Failed to set discharge current limit"));
        success = false;
    }
    
    if (success) {
        DBG.println(F("BQ24780S: Hardware configuration applied successfully"));
    } else {
        DBG.println(F("BQ24780S: Hardware configuration completed with errors"));
    }
    
    return success;
}

bool BQ24780S::isConnected() {
    if (i2c == nullptr) {
        return false;
    }
    
    // 检查缓存是否有效（1 秒内）
    unsigned long current_time = millis();
    if (current_time - last_connection_check_time_ < 1000UL) {
        // 缓存有效期内直接返回上次结果
        return last_connection_status_;
    }
    
    // 超过 1 秒，执行实际的 I2C 检测
    last_connection_status_ = i2c->isDeviceConnected(BQ24780SConst::ADDRESS);
    last_connection_check_time_ = current_time;
    
    return last_connection_status_;
}

bool BQ24780S::readRegister(uint8_t reg, uint16_t *value) {
    if (!initialized_ || i2c == nullptr) {
        return false;
    }
    
    bool result = i2c->readRegisterWord(BQ24780SConst::ADDRESS, reg, value);
    
    // 读取成功时刷新连接状态缓存
    if (result) {
        last_connection_status_ = true;
        last_connection_check_time_ = millis();
    }
    
    return result;
}

bool BQ24780S::writeRegister(uint8_t reg, uint16_t value) {
    if (!initialized_ || i2c == nullptr) {
        return false;
    }
    
    bool result = i2c->writeRegisterWord(BQ24780SConst::ADDRESS, reg, value);
    
    // 写入成功时刷新连接状态缓存
    if (result) {
        last_connection_status_ = true;
        last_connection_check_time_ = millis();
    }
    
    return result;
}

// =============================================================================
// Gain Configuration Methods
// =============================================================================

bool BQ24780S::setIadpGain(BQ24780SConst::IadpGain gain) {
    if (!initialized_) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION0, &reg_value)) {
        DBG.println(F("BQ24780S: Failed to read CHARGE_OPTION0 for IADP gain"));
        return false;
    }
    
    // 修改 Bit 4 (IADP gain)
    if (gain == BQ24780SConst::IadpGain::Gain40x) {
        reg_value |= 0x0010;  // Set bit 4
    } else {
        reg_value &= ~0x0010; // Clear bit 4
    }
    
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_OPTION0, reg_value);
    if (result) {
        current_iadp_gain_ = gain;
        DBG.printf_P(PSTR("BQ24780S: IADP gain set to %s (bit4=%d)\n"),
                      gain == BQ24780SConst::IadpGain::Gain20x ? "20x" : "40x",
                      (reg_value >> 4) & 0x01);
    } else {
        DBG.println(F("BQ24780S: Failed to write IADP gain"));
    }
    return result;
}

bool BQ24780S::setIdchgGain(BQ24780SConst::IdchgGain gain) {
    if (!initialized_) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION0, &reg_value)) {
        DBG.println(F("BQ24780S: Failed to read CHARGE_OPTION0 for IDCHG gain"));
        return false;
    }
    
    // 修改 Bit 3 (IDCHG gain)
    if (gain == BQ24780SConst::IdchgGain::Gain16x) {
        reg_value |= 0x0008;  // Set bit 3
    } else {
        reg_value &= ~0x0008; // Clear bit 3
    }
    
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_OPTION0, reg_value);
    if (result) {
        current_idchg_gain_ = gain;
        DBG.printf_P(PSTR("BQ24780S: IDCHG gain set to %s (bit3=%d)\n"),
                      gain == BQ24780SConst::IdchgGain::Gain8x ? "8x" : "16x",
                      (reg_value >> 3) & 0x01);
    } else {
        DBG.println(F("BQ24780S: Failed to write IDCHG gain"));
    }
    return result;
}

// =============================================================================
// Private Helper Methods - Configuration
// =============================================================================

bool BQ24780S::configureMonitoringOutputs(bool enable_idchg, bool enable_pmon) {
    if (!initialized_) {
        return false;
    }
    
    uint16_t reg_value;
    // 读取 CHARGE_OPTION1 (0x3B)
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION1, &reg_value)) {
        DBG.println(F("BQ24780S: Failed to read CHARGE_OPTION1 for monitoring config"));
        return false;
    }
    
    // 设置 Bit 11 (EN_IDCHG) 和 Bit 10 (EN_PMON)
    if (enable_idchg) {
        reg_value |= 0x0800;   // Set bit 11
    } else {
        reg_value &= ~0x0800;  // Clear bit 11
    }
    
    if (enable_pmon) {
        reg_value |= 0x0400;   // Set bit 10
    } else {
        reg_value &= ~0x0400;  // Clear bit 10
    }
    
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_OPTION1, reg_value);
    if (result) {
        DBG.printf_P(PSTR("BQ24780S: Monitoring outputs configured (EN_IDCHG=%d, EN_PMON=%d)\n"),
                      (reg_value >> 11) & 0x01,
                      (reg_value >> 10) & 0x01);
    } else {
        DBG.println(F("BQ24780S: Failed to configure monitoring outputs"));
    }
    return result;
}

bool BQ24780S::configureDischargeRegulation(bool enable) {
    if (!initialized_) {
        return false;
    }
    
    uint16_t reg_value;
    // 读取 CHARGE_OPTION3 (0x37)
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION3, &reg_value)) {
        DBG.println(F("BQ24780S: Failed to read CHARGE_OPTION3 for discharge regulation"));
        return false;
    }
    
    // 设置 Bit 15 (EN_IDCHG_REG)
    if (enable) {
        reg_value |= 0x8000;   // Set bit 15
    } else {
        reg_value &= ~0x8000;  // Clear bit 15
    }
    
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_OPTION3, reg_value);
    if (result) {
        DBG.printf_P(PSTR("BQ24780S: Discharge regulation %s (bit15=%d)\n"),
                      enable ? "enabled" : "disabled",
                      (reg_value >> 15) & 0x01);
    } else {
        DBG.println(F("BQ24780S: Failed to configure discharge regulation"));
    }
    return result;
}

bool BQ24780S::setHybridPowerBoost(bool enable) {
    if (!initialized_) {
        return false;
    }

    uint16_t reg_value;
    // 读取 CHARGE_OPTION3 (0x37)
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION3, &reg_value)) {
        DBG.println(F("BQ24780S: Failed to read CHARGE_OPTION3 for hybrid boost"));
        return false;
    }

    // 修改 Bit 2 (EN_BOOST): 0=Disable, 1=Enable
    if (enable) {
        reg_value |= 0x0004;   // Set bit 2
    } else {
        reg_value &= ~0x0004;  // Clear bit 2
    }

    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_OPTION3, reg_value);
    if (!result) {
        DBG.println(F("BQ24780S: Failed to set hybrid power boost"));
        return false;
    }

    // BQ24800: 额外设置 ChargeOption2 (0x38) 的 EN_BATT_BOOST (bit6) 和 VBOOST (bit5)
    if (chip_variant_ == BQ24780SConst::ChipVariant::BQ24800) {
        uint16_t reg2_value;
        if (readRegister(BQ24780SConst::Registers::CHARGE_OPTION2, &reg2_value)) {
            if (enable) {
                reg2_value |= (1 << BQ24780SConst::Registers::CHARGE_OPTION2_EN_BATT_BOOST_BIT);
            } else {
                reg2_value &= ~(1 << BQ24780SConst::Registers::CHARGE_OPTION2_EN_BATT_BOOST_BIT);
            }
            if (!writeRegister(BQ24780SConst::Registers::CHARGE_OPTION2, reg2_value)) {
                DBG.println(F("BQ24800: Failed to set EN_BATT_BOOST"));
            }
        }
    }

    DBG.printf_P(PSTR("BQ24780S: Hybrid power boost %s\n"), enable ? "enabled" : "disabled");
    return true;
}

bool BQ24780S::setVsysMin(uint16_t voltage_mV) {
    if (!initialized_ || chip_variant_ != BQ24780SConst::ChipVariant::BQ24800) {
        return false;
    }

    // 6-bit, bits [13:8], step 256mV
    uint16_t reg_value = (voltage_mV / BQ24780SConst::Voltage::VSYS_MIN_STEP) & 0x3F;
    reg_value <<= 8;

    bool result = writeRegister(BQ24780SConst::Registers::VSYS_MIN, reg_value);
    if (result) {
        DBG.printf_P(PSTR("BQ24800: VsysMin set to %u mV\n"), voltage_mV);
    } else {
        DBG.println(F("BQ24800: Failed to set VsysMin"));
    }
    return result;
}

bool BQ24780S::getVsysMin(uint16_t* voltage_mV) {
    if (!initialized_ || chip_variant_ != BQ24780SConst::ChipVariant::BQ24800 || voltage_mV == nullptr) {
        return false;
    }

    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::VSYS_MIN, &reg_value)) {
        return false;
    }

    uint16_t voltage_bits = (reg_value >> 8) & 0x3F;
    *voltage_mV = voltage_bits * BQ24780SConst::Voltage::VSYS_MIN_STEP;
    return true;
}

// =============================================================================
// Private Helper Methods
// =============================================================================

// 注意：getIadpGain() 和 getIdchgGain() 已移至 public 接口，返回枚举类型
// 旧的 float 版本已移除

uint16_t BQ24780S::calculateIadpCurrent(uint16_t voltage_mV) const {
    // 输入：mV, 输出：mA
    // I[mA] = V[mV] / (gain × Rsense[Ω])
    //       = V[mV] × 1000 / (gain × Rsense[mΩ])
    uint16_t gain = (current_iadp_gain_ == BQ24780SConst::IadpGain::Gain20x) ? 20 : 40;
    return (uint32_t)voltage_mV * 1000 / (gain * BQ24780SConst::Current::SENSE_RESISTOR);
}

uint16_t BQ24780S::calculateIdchgCurrent(uint16_t voltage_mV) const {
    // 输入：mV, 输出：mA
    // I[mA] = V[mV] / (gain × Rsense[Ω])
    //       = V[mV] × 1000 / (gain × Rsense[mΩ])
    uint16_t gain = (current_idchg_gain_ == BQ24780SConst::IdchgGain::Gain8x) ? 8 : 16;
    return (uint32_t)voltage_mV * 1000 / (gain * BQ24780SConst::Current::SENSE_RESISTOR);
}

uint32_t BQ24780S::calculatePmonPower(uint16_t voltage_mV) const {
    // 输入：mV, 输出：W
    // 根据 BQ24780S 数据手册：
    // PMON 引脚电压与功率成正比：Vpmon[mV] = P[mW] × Rpmon[kΩ] × Kpmon[μA/mW]
    // 其中：Kpmon = 1μA/mW (PMON_SCALE_FACTOR)
    //       Rpmon = 14.3kΩ (PMON_RESISTANCE)
    // 
    // 反推功率：P[mW] = Vpmon[mV] / (Rpmon[kΩ] × Kpmon[μA/mW])
    // 
    // 单位转换：Rpmon = 14300Ω = 14.3kΩ
    // 使用整数运算，避免浮点数：
    // P[W] = V[mV] / (R[kΩ] × SF[μA/W])
    //       = V[mV] × 1000 / (R[Ω] × SF[μA/W])
    
    const uint32_t RSNS_RATIO = 1;  // 假设 IADP/IDCHG 增益和采样电阻相同
    
    return (uint32_t)voltage_mV * 1000 / 
           (BQ24780SConst::Power::PMON_RESISTANCE * 
            BQ24780SConst::Power::PMON_SCALE_FACTOR * 
            RSNS_RATIO);
}

bool BQ24780S::setChargeCurrent(uint16_t current_mA) {
    if (!initialized_) {
        return false;
    }
    
    // 如果超过最大值，限制为最大值
    if (current_mA > BQ24780SConst::Current::CHARGE_CURRENT_MAX) {
        DBG.printf_P(PSTR("BQ24780S: Charge current %u mA exceeds maximum, limiting to %u mA\n"), 
                       current_mA, BQ24780SConst::Current::CHARGE_CURRENT_MAX);
        current_mA = BQ24780SConst::Current::CHARGE_CURRENT_MAX;
    }
    
    // 计算寄存器值
    uint16_t reg_value = (current_mA / static_cast<uint16_t>(BQ24780SConst::Current::CHARGE_CURRENT_STEP)) & 0x7F;
    reg_value <<= 6;
    
    // 直接写入硬件
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_CURRENT, reg_value);

    if (!result) {
        DBG.printf_P(PSTR("BQ24780S: Failed to set charge current to %u mA\n"), current_mA);
    }
    return result;
}

bool BQ24780S::getChargeCurrent(uint16_t* current_mA) {
    if (!initialized_ || current_mA == nullptr) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::CHARGE_CURRENT, &reg_value)) {
        return false;
    }
    
    // 提取电流值
    uint16_t current_bits = (reg_value >> 6) & 0x7F;
    *current_mA = current_bits * static_cast<uint16_t>(BQ24780SConst::Current::CHARGE_CURRENT_STEP);
    
    return true;
}

bool BQ24780S::setChargeVoltage(uint16_t voltage_mV) {
    if (!initialized_) {
        return false;
    }
    
    // 验证电压值范围
    if (voltage_mV > BQ24780SConst::Voltage::CHARGE_VOLTAGE_MAX) {
        DBG.printf_P(PSTR("BQ24780S: Charge voltage %u mV exceeds maximum %u mV\n"), 
                       voltage_mV, BQ24780SConst::Voltage::CHARGE_VOLTAGE_MAX);
        return false;
    }
    
    // 计算寄存器值
    uint16_t reg_value = (voltage_mV / static_cast<uint16_t>(BQ24780SConst::Voltage::CHARGE_VOLTAGE_STEP)) & 0x7FF;
    reg_value <<= 4;
    
    // 直接写入硬件
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_VOLTAGE, reg_value);

    if (!result) {
        DBG.printf_P(PSTR("BQ24780S: Failed to set charge voltage to %u mV\n"), voltage_mV);
    }
    return result;
}

bool BQ24780S::getChargeVoltage(uint16_t* voltage_mV) {
    if (!initialized_ || voltage_mV == nullptr) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::CHARGE_VOLTAGE, &reg_value)) {
        return false;
    }
    
    // 提取电压值
    uint16_t voltage_bits = (reg_value >> 4) & 0x7FF;
    *voltage_mV = voltage_bits * static_cast<uint16_t>(BQ24780SConst::Voltage::CHARGE_VOLTAGE_STEP);
    
    return true;
}

bool BQ24780S::setCharging(bool enable) {
    if (!initialized_) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION0, &reg_value)) {
        return false;
    }
    
    // 修改 CHRG_INHIBIT 位 (bit 0: 0=使能充电，1=抑制充电)
    if (enable) {
        reg_value &= ~0x01;  // 清除 bit 0
    } else {
        reg_value |= 0x01;   // 设置 bit 0
    }
    
    bool result = writeRegister(BQ24780SConst::Registers::CHARGE_OPTION0, reg_value);
    
    if (result) {
        DBG.printf_P(PSTR("BQ24780S: %s charging\n"), enable ? "Enabled" : "Disabled");
    } else {
        DBG.printf_P(PSTR("BQ24780S: Failed to %s charging\n"), enable ? "enable" : "disable");
    }
    return result;
}

bool BQ24780S::isChargingEnabled() {
    if (!initialized_) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::CHARGE_OPTION0, &reg_value)) {
        return false;
    }
    
    // CHRG_INHIBIT 是 bit 0: 0=使能充电，1=抑制充电
    return (reg_value & 0x01) == 0;
}

bool BQ24780S::setInputCurrentLimit(uint16_t current_mA) {
    if (!initialized_) {
        return false;
    }
    
    // 如果超过最大值，限制为最大值
    if (current_mA > BQ24780SConst::Current::INPUT_CURRENT_MAX) {
        DBG.printf_P(PSTR("BQ24780S: Input current limit %u mA exceeds maximum, limiting to %u mA\n"), 
                       current_mA, BQ24780SConst::Current::INPUT_CURRENT_MAX);
        current_mA = BQ24780SConst::Current::INPUT_CURRENT_MAX;
    }
    
    // 计算寄存器值
    uint16_t reg_value = (current_mA / static_cast<uint16_t>(BQ24780SConst::Current::INPUT_CURRENT_STEP)) & 0x3F;
    reg_value <<= 7;
    
    // 直接写入硬件
    bool result = writeRegister(BQ24780SConst::Registers::INPUT_CURRENT, reg_value);
    
    if (!result) {
        DBG.printf_P(PSTR("BQ24780S: Failed to set input current limit to %u mA\n"), current_mA);
    }
    return result;
}

bool BQ24780S::getInputCurrentLimit(uint16_t* current_mA) {
    if (!initialized_ || current_mA == nullptr) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::INPUT_CURRENT, &reg_value)) {
        return false;
    }
    
    // 提取电流值
    uint16_t current_bits = (reg_value >> 7) & 0x3F;
    *current_mA = current_bits * static_cast<uint16_t>(BQ24780SConst::Current::INPUT_CURRENT_STEP);
    
    return true;
}

bool BQ24780S::setDischargeCurrentLimit(uint16_t current_mA) {
    if (!initialized_) {
        return false;
    }
    
    // 如果超过最大值，限制为最大值
    if (current_mA > BQ24780SConst::Current::DISCHARGE_CURRENT_MAX) {
        DBG.printf_P(PSTR("BQ24780S: Discharge current limit %u mA exceeds maximum, limiting to %u mA\n"), 
                       current_mA, BQ24780SConst::Current::DISCHARGE_CURRENT_MAX);
        current_mA = BQ24780SConst::Current::DISCHARGE_CURRENT_MAX;
    }
    
    // 计算寄存器值
    uint16_t reg_value = (current_mA / static_cast<uint16_t>(BQ24780SConst::Current::DISCHARGE_CURRENT_STEP)) & 0x3F;
    reg_value <<= 9;
    
    // 直接写入硬件
    bool result = writeRegister(BQ24780SConst::Registers::DISCHARGE_CURRENT, reg_value);
    
    if (!result) {
        DBG.printf_P(PSTR("BQ24780S: Failed to set discharge current limit to %u mA\n"), current_mA);
    }
    return result;
}

bool BQ24780S::getDischargeCurrentLimit(uint16_t* current_mA) {
    if (!initialized_ || current_mA == nullptr) {
        return false;
    }
    
    uint16_t reg_value;
    if (!readRegister(BQ24780SConst::Registers::DISCHARGE_CURRENT, &reg_value)) {
        return false;
    }
    
    // 提取电流值
    uint16_t current_bits = (reg_value >> 9) & 0x3F;
    *current_mA = current_bits * static_cast<uint16_t>(BQ24780SConst::Current::DISCHARGE_CURRENT_STEP);
    
    return true;
}

// =============================================================================
// Status & Fault Management Methods
// =============================================================================

bool BQ24780S::readProchotStatus(uint16_t* status) {
    if (!initialized_ || status == nullptr) {
        return false;
    }
    
    uint16_t raw_status;
    bool result = readRegister(BQ24780SConst::Registers::PROCHOT_STATUS, &raw_status);
    
    if (result) {
        *status = raw_status & 0x007F;
        
        if (*status > 0) {
            DBG.print(F("BQ24780S: PROCHOT status = 0x"));
            DBG.print(*status, HEX);
            DBG.print(F(" | "));
            
            if (*status & 0x01) DBG.print(F("ACOK "));
            if (*status & 0x02) DBG.print(F("BATPRES "));
            if (*status & 0x04) DBG.print(F("VSYS "));
            if (*status & 0x08) DBG.print(F("IDCHG "));
            if (*status & 0x10) DBG.print(F("INOM "));
            if (*status & 0x20) DBG.print(F("ICRIT "));
            if (*status & 0x40) DBG.print(F("CMP "));
            
            DBG.println();
        }
    } else {
        DBG.println(F("BQ24780S: Failed to read PROCHOT status"));
    }
    
    return result;
}

bool BQ24780S::debugReadChargeRegisters() {
    if (!initialized_) {
        DBG.println(F("BQ24780S: Not initialized, cannot read registers"));
        return false;
    }
    
    DBG.println(F("\n========== BQ24780S Charge Registers Debug =========="));
    
    bool success = true;
    uint16_t reg_value;
    
    DBG.print(F("Reading CHARGE_VOLTAGE (0x15): "));
    if (readRegister(BQ24780SConst::Registers::CHARGE_VOLTAGE, &reg_value)) {
        DBG.printf(F("0x%04X"), reg_value);
        uint16_t voltage_bits = (reg_value >> 4) & 0x7FF;
        uint16_t voltage_mV = voltage_bits * static_cast<uint16_t>(BQ24780SConst::Voltage::CHARGE_VOLTAGE_STEP);
        DBG.printf(F("  -> Voltage: %u mV (%.3f V)\n"), voltage_mV, voltage_mV / 1000.0f);
    } else {
        DBG.println(F("FAILED"));
        success = false;
    }
    
    DBG.print(F("Reading CHARGE_CURRENT (0x14): "));
    if (readRegister(BQ24780SConst::Registers::CHARGE_CURRENT, &reg_value)) {
        DBG.printf(F("0x%04X"), reg_value);
        uint16_t current_bits = (reg_value >> 6) & 0x7F;
        uint16_t current_mA = current_bits * static_cast<uint16_t>(BQ24780SConst::Current::CHARGE_CURRENT_STEP);
        DBG.printf(F("  -> Current: %u mA (%.2f A)\n"), current_mA, current_mA / 1000.0f);
    } else {
        DBG.println(F("FAILED"));
        success = false;
    }
    
    DBG.print(F("Reading CHARGE_OPTION0 (0x12): "));
    if (readRegister(BQ24780SConst::Registers::CHARGE_OPTION0, &reg_value)) {
        DBG.printf(F("0x%04X"), reg_value);
        DBG.print(F("  -> "));
        DBG.print(F("CHRG_INHIBIT (Bit0): "));
        DBG.print((reg_value & 0x01) ? F("1 (Charging DISABLED)\n") : F("0 (Charging ENABLED)\n"));
        
        DBG.print(F("    IDCHG Gain (Bit3): "));
        DBG.println((reg_value & 0x08) ? F("16x") : F("8x"));
        
        DBG.print(F("    IADP Gain (Bit4): "));
        DBG.println((reg_value & 0x10) ? F("40x") : F("20x"));
    } else {
        DBG.println(F("FAILED"));
        success = false;
    }
    
    // CHARGE_OPTION2 (0x38) - EN_BATT_BOOST / VBOOST (BQ24800)
    if (chip_variant_ == BQ24780SConst::ChipVariant::BQ24800) {
        DBG.print(F("Reading CHARGE_OPTION2 (0x38): "));
        if (readRegister(BQ24780SConst::Registers::CHARGE_OPTION2, &reg_value)) {
            DBG.printf(F("0x%04X"), reg_value);
            DBG.print(F("  -> EN_BATT_BOOST (Bit6): "));
            DBG.println((reg_value & (1 << BQ24780SConst::Registers::CHARGE_OPTION2_EN_BATT_BOOST_BIT)) ? F("1 (Enabled)") : F("0 (Disabled)"));
            DBG.print(F("    VBOOST (Bit5): "));
            DBG.println((reg_value & (1 << BQ24780SConst::Registers::CHARGE_OPTION2_VBOOST_BIT)) ? F("1 (VSYSMIN+2.3V)") : F("0 (VSYSMIN+1.5V)"));
        } else {
            DBG.println(F("FAILED"));
            success = false;
        }

        // VSYS_MIN (0x3E)
        DBG.print(F("Reading VSYS_MIN (0x3E): "));
        if (readRegister(BQ24780SConst::Registers::VSYS_MIN, &reg_value)) {
            DBG.printf(F("0x%04X"), reg_value);
            uint16_t vsys_min_bits = (reg_value >> 8) & 0x3F;
            uint16_t vsys_min_mV = vsys_min_bits * BQ24780SConst::Voltage::VSYS_MIN_STEP;
            DBG.printf(F("  -> VsysMin: %u mV (%.3f V)\n"), vsys_min_mV, vsys_min_mV / 1000.0f);
        } else {
            DBG.println(F("FAILED"));
            success = false;
        }
    }

    if (success) {
        DBG.println(F("=====================================================\n"));
    } else {
        DBG.println(F("===== Some registers failed to read =====\n"));
    }

    return success;
}

bool BQ24780S::readAllRegisters(uint16_t* values) {
    if (!initialized_ || values == nullptr) {
        return false;
    }

    // 定义所有需要读取的寄存器地址
    const uint8_t registers[] = {
        BQ24780SConst::Registers::CHARGE_OPTION0,      // 0x12
        BQ24780SConst::Registers::CHARGE_OPTION1,      // 0x3B
        BQ24780SConst::Registers::CHARGE_OPTION2,      // 0x38
        BQ24780SConst::Registers::CHARGE_OPTION3,      // 0x37
        BQ24780SConst::Registers::PROCHOT_OPTION0,     // 0x3C
        BQ24780SConst::Registers::PROCHOT_OPTION1,     // 0x3D
        BQ24780SConst::Registers::PROCHOT_STATUS,      // 0x3A
        BQ24780SConst::Registers::CHARGE_CURRENT,      // 0x14
        BQ24780SConst::Registers::CHARGE_VOLTAGE,      // 0x15
        BQ24780SConst::Registers::DISCHARGE_CURRENT,   // 0x39
        BQ24780SConst::Registers::INPUT_CURRENT        // 0x3F
    };

    const int num_registers = sizeof(registers) / sizeof(registers[0]);

    // 逐个读取寄存器
    for (int i = 0; i < num_registers; i++) {
        if (!readRegister(registers[i], &values[i])) {
            DBG.printf_P(PSTR("BQ24780S: Failed to read register 0x%02X\n"), registers[i]);
            return false;
        }
    }

    // BQ24800: 额外读取 VsysMin (0x3E)
    if (chip_variant_ == BQ24780SConst::ChipVariant::BQ24800) {
        if (!readRegister(BQ24780SConst::Registers::VSYS_MIN, &values[11])) {
            values[11] = 0;
        }
    }
    
    return true;
}
