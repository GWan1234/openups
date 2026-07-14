#ifndef BMS_LFP_H
#define BMS_LFP_H

#include "bms.h"

// 磷酸铁锂 BMS：库仑计主导 + 锚点校准。
// 与 NCM 的本质差异：中段(约3.25-3.35V)OCV曲线斜率<0.5mV/%，
// 电压不可用于SOC估计，ocvToSoc()在平台区返回-1，一切修正只在两端进行。
class BMS_LFP : public BMS {
public:
    BMS_LFP(I2CInterface& i2c_interface, const BMS_Config_t& config)
        : BMS(i2c_interface, config) {}

    BatteryChemistry_t chemistry() const override { return CHEM_LFP; }

protected:
    float ocvToSoc(uint16_t cell_mv) const override;
    bool isFullChargeAnchor(const BMS_State& s, float cutoff_mA) const override;
    bool isEmptyAnchor(const BMS_State& s, float cutoff_mA) const override;
    float emptyAnchorSoc(const BMS_State& s) const override;
    void fuseSoc(BMS_State& s, float soc_coulomb, float q_max) override;
    bool fallbackWaitsForAnchor() const override { return true; }
    // 压差折算临时SOH对LFP平台区无意义（50mV压差可能对应30%也可能2%），禁用
    void updateTemporarySOH(BMS_State& bmsState) override {
        bmsState.temporary_soh = stats_.soh;
    }
    // 静置ΔSOC法：ΔSOC由库仑计推出再校库仑计=循环论证，禁用
    bool restSohLearningEnabled() const override { return false; }
    // 充电ΔSOC法同理禁用，SOH只在锚点间学习
    void detectChargeSOHLearning(BMS_State& bmsState) override {}
    void onFullChargeAnchor(BMS_State& s) override;
    void onEmptyDischargeAnchor(BMS_State& s) override;
    // 平台区两芯SOC差20%压差也只有几mV，均衡只在充电末端上升区有意义
    bool balancingPermitted(const BMS_State& s) const override {
        return s.cell_voltage_max > 3400;
    }

private:
    // 满充锚点要求 taper 持续 ≥60s（LFP CV段电压上冲快，瞬时高压≠充满）
    // isFullChargeAnchor 为 const，计时器用 mutable
    mutable unsigned long taper_start_ms_ = 0;

    // 锚点间 SOH 学习状态
    enum { ANCHOR_NONE = 0, ANCHOR_FULL = 1, ANCHOR_EMPTY = 2 };
    uint8_t last_anchor_ = ANCHOR_NONE;
    float last_empty_soc_ = 0.0f;   // 上次放空锚点锚定到的 SOC（0 或 5）

    // 底部OCV硬锚定：每个静置期最多一次（离开静置时复位）
    bool bottom_snap_done_ = false;

    float averageIR_mOhm() const;
    void settleAnchorSoh(bool ending_at_full);
};

#endif
