#ifndef BMS_NCM_H
#define BMS_NCM_H

#include "bms.h"

// 三元锂 BMS：现有算法原样承接（OCV 融合 / 充电 ΔSOC SOH 学习 / 压差临时 SOH）
class BMS_NCM : public BMS {
public:
    BMS_NCM(I2CInterface& i2c_interface, const BMS_Config_t& config)
        : BMS(i2c_interface, config) {}

    BatteryChemistry_t chemistry() const override { return CHEM_NCM; }

protected:
    float ocvToSoc(uint16_t cell_mv) const override;
    bool isFullChargeAnchor(const BMS_State& s, float cutoff_mA) const override;
    bool isEmptyAnchor(const BMS_State& s, float cutoff_mA) const override;
    void fuseSoc(BMS_State& s, float soc_coulomb, float q_max) override;
    void updateTemporarySOH(BMS_State& bmsState) override;
    void detectChargeSOHLearning(BMS_State& bmsState) override;
    void onFullChargeAnchor(BMS_State& s) override;
};

#endif
