#ifndef BATTERY_CHEMISTRY_H
#define BATTERY_CHEMISTRY_H

#include <stdint.h>

// 电池化学类型（持久化到 NVS，枚举值不得改动）
typedef enum : uint8_t {
    CHEM_NCM = 0,   // 三元锂（默认，兼容旧配置）
    CHEM_LFP = 1,   // 磷酸铁锂
} BatteryChemistry_t;

// 化学常量边界表 —— 只承载"数字边界"，供无 BMS 实例场景（Web 校验、
// 向导默认值、Power 默认值派生、HID）查询。算法差异在 BMS 子类中，
// 此结构不是运行时 profile。
typedef struct {
    // 单体电压有效窗口（采样有效性判断，非保护阈值）
    uint16_t valid_cell_min_mV;
    uint16_t valid_cell_max_mV;
    // Web 校验范围：OV/UV 阈值与恢复值的允许区间
    uint16_t ov_range_min_mV,  ov_range_max_mV;
    uint16_t ov_rec_min_mV,    ov_rec_max_mV;
    uint16_t uv_range_min_mV,  uv_range_max_mV;
    uint16_t uv_rec_min_mV,    uv_rec_max_mV;
    // 推荐值（切换化学时自动填充）
    uint16_t recommended_ov_mV;
    uint16_t recommended_ov_rec_mV;
    uint16_t recommended_uv_mV;
    uint16_t recommended_uv_rec_mV;
    uint16_t recommended_charge_cell_mV;  // 推荐单体充电电压
    uint16_t nominal_cell_mV;             // 标称电压（HID 用）
    // 内阻突变分析的最小可信 ΔI（LFP 内阻小，需更大电流突变才有信噪比）
    int16_t  burst_min_delta_mA;
    const char* name;                     // "NCM" / "LiFePO4"
} ChemistryLimits;

// 按化学类型取常量表（实现在 bms.cpp 末尾，非法值回退 NCM）
const ChemistryLimits& getChemistryLimits(BatteryChemistry_t chem);

#endif
