#include "bms_ncm.h"
#include <Arduino.h>
#include "debug.h"

// OCV-SOC lookup table (NCM, 密集采样)
// 基于实际NCM电池放电曲线，中段(20-80%)每3-5%一个点，底部加密
static const uint16_t OCV_SOC_TABLE[][2] = {
    //  mV     SOC%
    {4200, 100},
    {4185,  98},
    {4170,  97},
    {4150,  96},
    {4125,  93},
    {4100,  91},
    {4075,  89},
    {4050,  87},
    {4025,  84},
    {4000,  82},
    {3959,  79},
    {3949,  78},
    {3932,  75},
    {3923,  73},
    {3912,  70},
    {3906,  68},
    {3898,  65},
    {3892,  63},
    {3882,  60},
    {3874,  58},
    {3859,  55},
    {3839,  52},
    {3813,  49},
    {3777,  47},
    {3716,  44},
    {3690,  41},
    {3675,  38},
    {3660,  35},
    {3645,  31},
    {3630,  28},
    {3610,  24},
    {3590,  21},
    {3565,  18},
    {3540,  15},
    {3510,  12},
    {3480,  10},
    {3415,   7},
    {3350,   5},
    {3250,   3},
    {3150,   2},
    {3060,   1},
    {3000,   0}
};
static const int OCV_TABLE_SIZE = sizeof(OCV_SOC_TABLE) / sizeof(OCV_SOC_TABLE[0]);

float BMS_NCM::ocvToSoc(uint16_t cell_mv) const {
    if (cell_mv <= OCV_SOC_TABLE[OCV_TABLE_SIZE - 1][0]) return 0.0f;
    if (cell_mv >= OCV_SOC_TABLE[0][0]) return 100.0f;

    for (int i = 0; i < OCV_TABLE_SIZE - 1; i++) {
        uint16_t v1 = OCV_SOC_TABLE[i][0];
        uint16_t soc1 = OCV_SOC_TABLE[i][1];
        uint16_t v2 = OCV_SOC_TABLE[i+1][0];
        uint16_t soc2 = OCV_SOC_TABLE[i+1][1];
        if (cell_mv >= v2 && cell_mv <= v1) {
            float soc = soc2 + (cell_mv - v2) * (soc1 - soc2) / (float)(v1 - v2);
            return constrain(soc, 0.0f, 100.0f);
        }
    }
    return -1.0f;
}

// 满充：最高单体 >4150mV 且 0 < I < C/20（CV taper）
bool BMS_NCM::isFullChargeAnchor(const BMS_State& s, float cutoff_mA) const {
    return s.cell_voltage_max > 4150 && s.current > 0 && s.current < cutoff_mA;
}

// 放空：最低单体 <3000mV 且放电小电流
bool BMS_NCM::isEmptyAnchor(const BMS_State& s, float cutoff_mA) const {
    return s.cell_voltage_min < 3000 && s.current < 0 && abs(s.current) < cutoff_mA;
}

void BMS_NCM::fuseSoc(BMS_State& bmsState, float soc_coulomb, float q_max) {
    float soc_voltage = calculateSOC_Voltage(bmsState);
    float cutoff_current = config_.nominal_capacity_mAh / 20.0f;

    // 满充锚定：高压 + 小电流充电
    if (isFullChargeAnchor(bmsState, cutoff_current)) {
        current_remaining_capacity = q_max;
        last_stable_soc_ = 100.0f;
        DBG.println(F("BMS: SOC anchored to 100% (CV taper)"));
    }
    // 放空锚定：低压 + 小电流放电
    else if (isEmptyAnchor(bmsState, cutoff_current)) {
        current_remaining_capacity = 0;
        last_stable_soc_ = 0.0f;
        DBG.println(F("BMS: SOC anchored to 0% (cutoff)"));
    }
    // 低电流时用电压收敛
    else if (abs(bmsState.current) < cutoff_current && soc_voltage > 0) {
        float diff = soc_voltage - soc_coulomb;
        if (abs(diff) > 2.0f) {
            float convergence_rate = 0.02f;  // 2% per update
            float delta_cap = (diff / 100.0f) * q_max * convergence_rate;
            current_remaining_capacity += delta_cap;
            current_remaining_capacity = constrain(current_remaining_capacity, 0.0f, q_max);
        }
        // NCM全程OCV可信，电压收敛持续压制真实误差——预算随之衰减，
        // 下限3%（OCV法自身精度），避免误报校准建议
        if (soc_error_est_pct_ > 3.0f) {
            soc_error_est_pct_ = 3.0f + (soc_error_est_pct_ - 3.0f) * 0.98f;
        }
        last_stable_soc_ = (current_remaining_capacity / q_max) * 100.0f;
    }
    // 充放电中，信任库仑计
    else {
        last_stable_soc_ = constrain(soc_coulomb, 0.0f, 100.0f);
    }
}

void BMS_NCM::updateTemporarySOH(BMS_State& bmsState) {
    uint16_t voltage_diff = bmsState.cell_voltage_max - bmsState.cell_voltage_min;

    // 压差迟滞：进入阈值50mV，退出阈值40mV，避免在临界点反复切换
    if (temporary_soh_active_) {
        if (voltage_diff < 40) {
            temporary_soh_active_ = false;
            bmsState.temporary_soh = stats_.soh;
            return;
        }
    } else {
        if (voltage_diff < 50) {
            bmsState.temporary_soh = stats_.soh;
            return;
        }
        temporary_soh_active_ = true;
    }

    float soc_min = calculateSOC_FromVoltage(bmsState.cell_voltage_min);
    float soc_max = calculateSOC_FromVoltage(bmsState.cell_voltage_max);

    // 电压无效时，使用原始SOH
    if (soc_min < 0 || soc_max < 0) {
        bmsState.temporary_soh = stats_.soh;
        return;
    }

    float charge_space = 1.0f - (soc_max / 100.0f);
    float discharge_space = soc_min / 100.0f;
    float effective_capacity_ratio = charge_space + discharge_space;

    bmsState.temporary_soh = stats_.soh * effective_capacity_ratio;
}

void BMS_NCM::detectChargeSOHLearning(BMS_State& bmsState) {
    float cutoff_current = config_.nominal_capacity_mAh / 20.0f;
    
    if (bmsState.current > cutoff_current) {
        // 正在充电
        if (!charge_soh_tracking_) {
            // 充电刚开始，记录起点，重置CC累积（排除放电阶段的零漂累积）
            charge_soh_tracking_ = true;
            charge_soc_start_ = bmsState.soc;
            cc_accumulated_raw_mAh_ = 0.0f;
            charge_cc_raw_start_ = 0.0f;
            DBG.printf_P(PSTR("BMS: Charge SOH tracking started at SOC=%.1f%%\n"), charge_soc_start_);
        }
    } else if (bmsState.current <= 0) {
        // 充电停止：完成SOH计算（不再依赖满充）
        if (charge_soh_tracking_) {
            float delta_ah_raw = cc_accumulated_raw_mAh_ - charge_cc_raw_start_;
            float delta_soc = bmsState.soc - charge_soc_start_;
            float q_nominal = (float)config_.nominal_capacity_mAh;

            if (delta_soc >= 20.0f && delta_ah_raw > 10.0f) {
                float q_actual = delta_ah_raw / (delta_soc / 100.0f);
                float soh_calc = (q_actual / q_nominal) * 100.0f;
                float soh_new = 0.7f * stats_.soh + 0.3f * soh_calc;
                soh_new = constrain(soh_new, 40.0f, 100.0f);

                DBG.printf_P(PSTR("BMS: Charge SOH learned (end): dSOC=%.1f%% dAh_raw=%.1f "
                    "Q_act=%.1f SOH_calc=%.1f%% -> SOH=%.1f%%\n"),
                    delta_soc, delta_ah_raw, q_actual, soh_calc, soh_new);

                stats_.soh = soh_new;
                bmsState.soh = stats_.soh;
                saveToStorage();
            } else {
                DBG.printf_P(PSTR("BMS: Charge SOH skipped (dSOC=%.1f%%, dAh=%.1f)\n"),
                    delta_soc, delta_ah_raw);
            }

            charge_soh_tracking_ = false;
        }
    }
}

void BMS_NCM::onFullChargeAnchor(BMS_State& bmsState) {
    // 充电阶段SOH学习：在基类重置 cc_accumulated_raw_mAh_ 之前结算
    if (charge_soh_tracking_) {
        float delta_ah_raw = cc_accumulated_raw_mAh_ - charge_cc_raw_start_;
        float delta_soc = 100.0f - charge_soc_start_;
        float q_nominal = (float)config_.nominal_capacity_mAh;

        if (delta_soc > 1.0f && delta_ah_raw > 10.0f) {
            float q_actual = delta_ah_raw / (delta_soc / 100.0f);
            float soh_calc = (q_actual / q_nominal) * 100.0f;
            float soh_new = 0.7f * stats_.soh + 0.3f * soh_calc;
            soh_new = constrain(soh_new, 40.0f, 100.0f);
            DBG.printf_P(PSTR("BMS: Charge SOH learned: dSOC=%.1f%% dAh_raw=%.1f "
                "Q_act=%.1f SOH_calc=%.1f%% -> SOH=%.1f%%\n"),
                delta_soc, delta_ah_raw, q_actual, soh_calc, soh_new);
            stats_.soh = soh_new;
            bmsState.soh = stats_.soh;
        }
        charge_soh_tracking_ = false;
    }
}
