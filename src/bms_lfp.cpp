#include "bms_lfp.h"
#include <Arduino.h>
#include <math.h>
#include "debug.h"

// LFP OCV-SOC 表：只在两端可信。
// 平台区(3250-3350mV, 约22-87%)斜率过小，查表误差可达±20%，直接返回-1拒绝估计。
// 调用方（SOC初始化、收敛修正）拿到-1自动回退库仑计/50%默认——无需任何LFP特判。
static const uint16_t LFP_OCV_TABLE[][2] = {
    //  mV    SOC%
    {3650, 100}, {3500, 99}, {3450, 97}, {3400, 94}, {3370, 90}, {3350, 87},
    // ---- 3250-3350 平台区：不建表 ----
    {3250, 22}, {3230, 17}, {3200, 12}, {3150, 8}, {3100, 5},
    {3000, 3},  {2900, 2},  {2800, 1},  {2500, 0},
};
static const int LFP_OCV_TABLE_SIZE = sizeof(LFP_OCV_TABLE) / sizeof(LFP_OCV_TABLE[0]);

float BMS_LFP::ocvToSoc(uint16_t cell_mv) const {
    // 平台区拒绝估计（3350/3250 边界值本身可用）
    if (cell_mv < 3350 && cell_mv > 3250) return -1.0f;

    if (cell_mv <= LFP_OCV_TABLE[LFP_OCV_TABLE_SIZE - 1][0]) return 0.0f;
    if (cell_mv >= LFP_OCV_TABLE[0][0]) return 100.0f;

    for (int i = 0; i < LFP_OCV_TABLE_SIZE - 1; i++) {
        uint16_t v1 = LFP_OCV_TABLE[i][0];
        uint16_t soc1 = LFP_OCV_TABLE[i][1];
        uint16_t v2 = LFP_OCV_TABLE[i+1][0];
        uint16_t soc2 = LFP_OCV_TABLE[i+1][1];
        if (cell_mv >= v2 && cell_mv <= v1) {
            float soc = soc2 + (cell_mv - v2) * (soc1 - soc2) / (float)(v1 - v2);
            return constrain(soc, 0.0f, 100.0f);
        }
    }
    return -1.0f;
}

// 满充锚点：max>3450mV + 0<I<C/20 且该状态持续≥60秒
bool BMS_LFP::isFullChargeAnchor(const BMS_State& s, float cutoff_mA) const {
    bool taper = (s.cell_voltage_max > 3450 && s.current > 0 && s.current < cutoff_mA);
    if (!taper) {
        taper_start_ms_ = 0;
        return false;
    }
    if (taper_start_ms_ == 0) {
        taper_start_ms_ = millis();
        return false;
    }
    return (millis() - taper_start_ms_ >= 60000);
}

// 无内阻测量结果时的保守缺省值 (mΩ)
static const float LFP_DEFAULT_IR_MOHM = 25.0f;

float BMS_LFP::averageIR_mOhm() const {
    const float* ir = getInternalResistance();
    float sum = 0.0f;
    uint8_t cnt = 0;
    for (uint8_t i = 0; i < config_.cell_count && i < 5; i++) {
        if (ir[i] > 0.5f && ir[i] < 500.0f) { sum += ir[i]; cnt++; }
    }
    return (cnt > 0) ? (sum / cnt) : LFP_DEFAULT_IR_MOHM;
}

// 放空锚点：LFP膝点后跳水极快，不要求小电流。
// 小电流：min<2800 直接锚0%；大电流：IR补偿后仍<2750 才锚（锚5%，见emptyAnchorSoc）
bool BMS_LFP::isEmptyAnchor(const BMS_State& s, float cutoff_mA) const {
    if (s.current >= 0 || s.cell_voltage_min >= 2800) return false;
    if (abs(s.current) < cutoff_mA) return true;
    float comp_mv = (float)s.cell_voltage_min +
                    fabsf((float)s.current) * averageIR_mOhm() / 1000.0f;
    return comp_mv < 2750.0f;
}

float BMS_LFP::emptyAnchorSoc(const BMS_State& s) const {
    float cutoff = config_.nominal_capacity_mAh / 20.0f;
    // 大电流下碰到2800只说明"快空了"，IR补偿有残余误差，锚5%留余量
    return (abs(s.current) < cutoff) ? 0.0f : 5.0f;
}

// SOC融合：库仑计为主，OCV修正严格受限
void BMS_LFP::fuseSoc(BMS_State& bmsState, float soc_coulomb, float q_max) {
    float cutoff_current = config_.nominal_capacity_mAh / 20.0f;

    if (isFullChargeAnchor(bmsState, cutoff_current)) {
        current_remaining_capacity = q_max;
        last_stable_soc_ = 100.0f;
        DBG.println(F("BMS_LFP: SOC anchored to 100% (taper 60s)"));
        return;
    }
    if (isEmptyAnchor(bmsState, cutoff_current)) {
        float anchor_soc = emptyAnchorSoc(bmsState);
        current_remaining_capacity = q_max * anchor_soc / 100.0f;
        last_stable_soc_ = anchor_soc;
        DBG.printf_P(PSTR("BMS_LFP: SOC anchored to %.0f%% (empty)\n"), anchor_soc);
        return;
    }

    // OCV 修正的准入条件（全部满足才修）：
    // 1. 静置已持续≥30分钟（LFP充放电迟滞20-30mV，静置不足的"OCV"不是OCV）
    // 2. ocvToSoc 可信（电压在两端陡峭区，平台区返回-1自动跳过）
    // 3. 压差<30mV（避免弱电芯拖偏）
    bool quiescent_long = is_quiescent_ && quiescent_start_time_ > 0 &&
                          (millis() - quiescent_start_time_ >= 30UL * 60UL * 1000UL);
    bool diff_ok = (bmsState.cell_voltage_max - bmsState.cell_voltage_min) < 30;
    float soc_voltage = (quiescent_long && diff_ok) ? calculateSOC_Voltage(bmsState) : -1.0f;

    if (soc_voltage >= 0.0f) {
        float diff = soc_voltage - soc_coulomb;
        // 非对称信任：迟滞总是偏向"来时方向"。
        // 顶部陡峭区只能是充电后到达——满充弛豫到3.40-3.45V，查表94-97%
        // 低于真实值，只许向上修（否则真100%被拖到~95%）；
        // 底部膝区只能是放电后到达——回弹电压查表偏高，只许向下修。
        bool top_region = (bmsState.cell_voltage_avg >= 3350);
        if (( top_region && diff < 0.0f) ||
            (!top_region && diff > 0.0f)) {
            diff = 0.0f;
        }

        // 机会性底部硬锚定：停电放电天然路过下膝区（<3250mV陡峭段），
        // 静置30分钟后OCV可信——每次静置期做一次一次性重置而非慢收敛，
        // 把停电事件的免费再校准价值吃满（方向门卫同上，只许向下）
        if (!top_region && diff < -2.0f && !bottom_snap_done_) {
            current_remaining_capacity = q_max * soc_voltage / 100.0f;
            bottom_snap_done_ = true;
            soc_error_est_pct_ = 3.0f;
            DBG.printf_P(PSTR("BMS_LFP: bottom OCV snap %.1f%% -> %.1f%%\n"),
                soc_coulomb, soc_voltage);
        } else if (fabsf(diff) > 2.0f) {
            float convergence_rate = 0.002f;   // NCM 的 1/10
            float delta_cap = (diff / 100.0f) * q_max * convergence_rate;
            current_remaining_capacity += delta_cap;
            current_remaining_capacity = constrain(current_remaining_capacity, 0.0f, q_max);
        }
        last_stable_soc_ = (current_remaining_capacity / q_max) * 100.0f;
    } else {
        // 其余全部时间：SOC = 库仑计，不做任何电压修正。
        // 离开静置后允许下一次静置期再做底部硬锚定
        if (!is_quiescent_) bottom_snap_done_ = false;
        last_stable_soc_ = constrain(soc_coulomb, 0.0f, 100.0f);
    }
}

// ============================ 锚点间 SOH 学习 ============================
// 只结算 满充→放空 / 放空→满充 区间：Q_act = 区间Ah积分 / ΔSOC。
// cc_accumulated_raw_mAh_ 由基类在每次锚点事件后清零，且LFP禁用了其他清零路径
// （restSohLearningEnabled=false、detectChargeSOHLearning空实现），
// 因此锚点时刻 |cc_accumulated_raw_mAh_| 即上一锚点以来的Ah积分。

void BMS_LFP::settleAnchorSoh(bool ending_at_full) {
    bool valid_pair = (ending_at_full  && last_anchor_ == ANCHOR_EMPTY) ||
                      (!ending_at_full && last_anchor_ == ANCHOR_FULL);
    if (!valid_pair) return;

    float delta_ah = fabsf(cc_accumulated_raw_mAh_);
    float delta_soc = 100.0f - last_empty_soc_;   // 满↔空区间的SOC跨度（95或100）
    float q_nominal = (float)config_.nominal_capacity_mAh;
    if (delta_soc < 50.0f || delta_ah < 10.0f) return;

    float q_actual = delta_ah / (delta_soc / 100.0f);
    // 异常结果丢弃（长时间跨度自放电误差/漏计等）
    if (q_actual < 0.3f * q_nominal || q_actual > 1.2f * q_nominal) {
        DBG.printf_P(PSTR("BMS_LFP: anchor SOH discarded (Q_act=%.0f)\n"), q_actual);
        return;
    }

    float soh_calc = (q_actual / q_nominal) * 100.0f;
    // 锚点间学习置信度高，EMA权重0.7/0.3（比NCM静置法的0.95/0.05激进）
    float soh_new = constrain(0.7f * stats_.soh + 0.3f * soh_calc, 40.0f, 100.0f);
    DBG.printf_P(PSTR("BMS_LFP: anchor SOH learned: dAh=%.1f dSOC=%.0f%% "
        "Q_act=%.1f SOH_calc=%.1f%% -> SOH=%.1f%%\n"),
        delta_ah, delta_soc, q_actual, soh_calc, soh_new);
    stats_.soh = soh_new;
    // 持久化由基类锚点流程中的 saveToStorage() 完成
}

void BMS_LFP::onFullChargeAnchor(BMS_State& s) {
    // 顶部压差失配监测：平台区压差检测失效后，满充时刻压差是仅剩的失配观测点
    uint16_t diff = s.cell_voltage_max - s.cell_voltage_min;
    if (diff > 80) {
        DBG.printf_P(PSTR("BMS_LFP: TIP top imbalance %umV (>80mV), check cells/IR\n"), diff);
    }
    settleAnchorSoh(true);
    last_anchor_ = ANCHOR_FULL;
}

void BMS_LFP::onEmptyDischargeAnchor(BMS_State& s) {
    last_empty_soc_ = emptyAnchorSoc(s);  // 先更新，settleAnchorSoh 需要用到
    settleAnchorSoh(false);
    last_anchor_ = ANCHOR_EMPTY;
}
