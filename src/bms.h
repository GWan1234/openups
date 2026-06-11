#ifndef BMS_H
#define BMS_H

#include <stdint.h>
#include <stdbool.h>
#include <Preferences.h>
#include "bq76920.h"
#include "i2c_interface.h"
#include "data_structures.h"
#include "event_types.h"
#include "pins_config.h"

#define RAW_BUFFER_SIZE 60

#define BMS_PREFS_NAMESPACE     "bms_data"
#define PREFS_KEY_SOH           "soh"
#define PREFS_KEY_CYCLES        "cycles"
#define PREFS_KEY_LAST_FAULT    "last_fault"
#define PREFS_KEY_BAL_EVENTS    "bal_events"
#define PREFS_KEY_LAST_FAULT_TIME "last_fault_time"
#define PREFS_KEY_REMAINING_CAP "bms_rem_cap"
#define PREFS_KEY_ACC_CHARGE    "bms_acc_ch"
#define PREFS_KEY_ACC_DISCHARGE "bms_acc_dch"
#define PREFS_KEY_CC_RAW_MAH    "bms_cc_raw"
#define PREFS_KEY_SOH_LEARNING  "bms_soh_lr"
#define PREFS_KEY_SOH_SOC_START "bms_soh_ss"
#define PREFS_KEY_SOH_AH_START  "bms_soh_as"
#define PREFS_KEY_SOH_LRN_TIME  "bms_soh_lt"
#define PREFS_KEY_PARTIAL_CYCLES "bms_pcycle"
#define PREFS_KEY_CHG_SOH_TRACK "bms_chg_tr"
#define PREFS_KEY_CHG_SOH_SOC   "bms_chg_ss"
#define PREFS_KEY_CHG_SOH_CC    "bms_chg_cc"

// 自消耗计算 NVS keys
#define PREFS_KEY_SELF_CONSUMP  "sc_mA"
#define PREFS_KEY_SC_SEG_COUNT  "sc_seg_cnt"
#define PREFS_KEY_SC_TOTAL_SEG  "sc_total_seg"
#define PREFS_KEY_SC_CONFIDENCE "sc_conf"
#define PREFS_KEY_SC_LAST_UPD   "sc_last_up"
#define PREFS_KEY_SC_LAST_CHK   "sc_last_chk"

#define PREFS_KEY_IR_RESULT     "ir_result"    // float[5] 共20字节
#define PREFS_KEY_IR_COUNT      "ir_count"

// OCV-SOC lookup table (NCM, 密集采样)
// 基于实际NCM电池放电曲线，中段(20-80%)每3-5%一个点，底部加密
const uint16_t OCV_SOC_TABLE[][2] = {
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
    {3975,  79},
    {3950,  78},
    {3925,  75},
    {3900,  73},
    {3875,  70},
    {3850,  68},
    {3830,  65},
    {3810,  63},
    {3795,  60},
    {3780,  58},
    {3765,  55},
    {3750,  52},
    {3735,  49},
    {3720,  47},
    {3705,  44},
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
const int OCV_TABLE_SIZE = sizeof(OCV_SOC_TABLE) / sizeof(OCV_SOC_TABLE[0]);

typedef enum {
    BMS_MODE_NORMAL = 0,
    BMS_MODE_FAULT,
} BMS_Mode_t;

typedef struct {
    float soh;
    uint32_t total_cycles;
    uint64_t balancing_events;
    BMS_Fault_t last_fault;
    uint32_t last_fault_time;
} BMS_Statistics_t;

// SOH Learning Context for capacity-based SOH estimation
typedef struct {
    bool is_learning;
    float soc_start;            // 学习开始时的 SOC (%)
    float ah_start;             // 学习开始时的原始库仑计累积值 (mAh)
    unsigned long learning_start_time;
} SOH_Learning_Context_t;

typedef struct {
    uint8_t cell_count;
    uint32_t nominal_capacity_mAh;
    uint16_t cell_ov_threshold;
    uint16_t cell_uv_threshold;
    uint16_t cell_ov_recover;
    uint16_t cell_uv_recover;
    uint16_t max_charge_current;
    uint16_t max_discharge_current;
    uint16_t short_circuit_threshold;
    float temp_overheat_threshold;
    bool balancing_enabled;
    float balancing_voltage_diff;
} BMS_Config_t;

// FET操作队列（解决I2C电源关闭时外部调用的竞态问题）
enum PendingFETAction {
    FET_ACTION_NONE = 0,
    FET_ACTION_DISABLE_DISCHARGE,
    FET_ACTION_ENABLE_DISCHARGE,
    FET_ACTION_DISABLE_CHARGE,
    FET_ACTION_ENABLE_CHARGE,
    FET_ACTION_ENTER_SHIP_MODE,
    FET_ACTION_EMERGENCY_SHUTDOWN,
};

class BMS {
public:
    explicit BMS(I2CInterface& i2c_interface, const BMS_Config_t& config);
    
    bool begin();
    void update(System_Global_State& globalState);
    
    // 声明静态指针，用于存储当前实例
    static BMS* instancePtr;
    
private:
    void processAlertStatus(uint8_t fault_reg);
    
public:

    // DFET Control
    bool disableDischarge();
    bool enableDischarge();

    // CFET Control
    bool disableCharge();
    bool enableCharge();

    bool enterShipMode();
    bool isInitialized() const { return initialized_; }
    bool startBalancing(BMS_State& bmsState);
    bool stopBalancing(BMS_State& bmsState);
    bool clearFault();
    bool emergencyShutdown();
    BMS_Fault_t translateChipFault(uint8_t fault_register);
    bool saveToStorage();
    bool loadFromStorage();
    bool resetBatteryData();
    static BMS_Config_t getDefaultConfig(uint8_t cell_count);

    // 自消耗计算结果 getter
    float getSelfConsumption_mA() const { return self_consumption_mA_; }
    uint8_t getSCSegmentCount() const { return sc_segment_count_; }
    uint8_t getSCTotalSegments() const { return sc_total_segments_; }
    uint8_t getSCConfidence() const { return sc_confidence_; }
    uint32_t getSCLastUpdate() const { return sc_last_update_; }
    uint32_t getSCLastCheck() const { return sc_last_check_; }

    // 内阻估算结果 getter
    const float* getInternalResistance() const { return ir_result_mΩ_; }
    uint8_t getIRSampleCount() const { return ir_sample_count_; }


    // 内阻估算结果 setter（由 SystemManager 分析任务调用）
    void updateInternalResistance(const float* ir_data, uint8_t sample_count) {
        for (uint8_t i = 0; i < 5; i++) ir_result_mΩ_[i] = ir_data[i];
        ir_sample_count_ = sample_count;
        saveIRData();
    }

    // 自消耗计算结果 setter（由 SystemManager 分析任务调用）
    void updateSelfConsumption(float mA, uint8_t seg_count, uint8_t total_segments, uint8_t confidence, uint32_t update_time, uint32_t check_time) {
        self_consumption_mA_ = mA;
        sc_segment_count_ = seg_count;
        sc_total_segments_ = total_segments;
        sc_confidence_ = confidence;
        sc_last_update_ = update_time;
        sc_last_check_ = check_time;
        saveSelfConsumption();
    }

    // 仅更新检查时间和总段数（分析未成功时调用）
    void updateSCLastCheck(uint32_t check_time, uint8_t total_segments = 0) {
        sc_last_check_ = check_time;
        if (total_segments > 0) sc_total_segments_ = total_segments;
    }
    bool applyNewConfig(const BMS_Config_t& config);
    void applyPendingConfig();

    BQ76920 bq76920_;
    BMS_Config_t config_;
    BMS_Statistics_t stats_;
    Preferences preferences_;
    
    bool initialized_;
    bool available_;
    bool discharge_enabled_;
    bool charge_enabled_;
    uint8_t i2c_failure_count_;
    bool i2c_power_enabled_;
    
    unsigned long last_fast_update_;
    unsigned long last_medium_update_;
    unsigned long last_slow_update_;
    unsigned long last_periodic_update_;
    unsigned long last_day_update_;
    unsigned long last_cc_update_;
    
    float current_remaining_capacity;
    float accumulated_charge_mAh;
    float accumulated_discharge_mAh;
    bool cc_ready_pending_;
    
    bool soc_initialized_;
    float last_stable_soc_;
    unsigned long last_soc_update_timestamp_;

    // SOH Learning context
    SOH_Learning_Context_t soh_learning_ctx_;
    unsigned long soc_stable_start_time_;
    bool soc_waiting_for_stable_;

    BMS_Fault_t hardware_fault_wait_;
    
    // 静置/活跃状态检测
    bool is_quiescent_;                      // 当前是否处于静置状态
    unsigned long quiescent_start_time_;     // 进入静置状态的开始时间

    // 临时SOH压差阈值迟滞状态
    bool temporary_soh_active_;              // 当前是否启用了临时SOH校正
    static const int QUIESCENT_CURRENT_THRESHOLD = 5;  // 静置电流阈值(mA)
    static const unsigned long QUIESCENT_TIME_THRESHOLD = 10000; // 静置时间阈值(ms)
    void updateQuiescentState(const BMS_State& bmsState);

    // 满充/放空校准锚点
    bool full_charge_calibrated_;
    bool empty_discharge_calibrated_;
    unsigned long last_full_charge_time_;
    unsigned long last_empty_discharge_time_;
    
    // SOH学习专用的原始库仑计累积(不受SOH缩放影响)
    float cc_accumulated_raw_mAh_;
    
    // 充电阶段SOH学习
    bool charge_soh_tracking_;
    float charge_soc_start_;
    float charge_cc_raw_start_;
    
    // 循环计数-分数累积
    float partial_cycles_;
    
    // 异步配置更新标记
    bool config_update_pending_;
    BMS_Config_t pending_config_;

    // FET操作队列
    volatile PendingFETAction pending_fet_action_;
    void processPendingFETAction();
    
    void updateSOC(BMS_State& bmsState);
    void updateFaultLogic(BMS_State& bmsState);
    void checkCriticalFaults(BMS_State& bmsState);
    void evaluateAndExecuteBalancing(BMS_State& bmsState);
    bool updateBasicInfo(BMS_State& bmsState);
    bool validateConfig(const BMS_Config_t& config);
    
    void handleBmsAlertInterrupt(uint8_t call_tag = 0);
    void handleCommunicationLoss(BMS_State& bmsState);
    
    float calculateSOC_Voltage(const BMS_State& bmsState);
    float calculateSOC_FromVoltage(uint16_t voltage_mv);
    float calculateSOC_Coulomb();
    float getAvailableCapacity() const;
    float getTemperatureCompensatedCapacity(float temperature) const;
    void updateTemporarySOH(BMS_State& bmsState);
    
    bool processCoulombCounterData();
    void compensateSelfDischarge(unsigned long delta_time_ms);
    void detectFullChargeCalibration(BMS_State& bmsState);
    void detectEmptyDischargeCalibration(BMS_State& bmsState);
    void updateSOHLearning(BMS_State& bmsState);
    void detectChargeSOHLearning(BMS_State& bmsState);
    void accumulatePartialCycle(float delta_mah);
    BQ76920_InitConfig generateChipConfig(const BMS_Config_t& config);
    
    // I2C隔离芯片电源控制
    inline void i2cPowerOn() {
        if (!i2c_power_enabled_) {
            digitalWrite(BQ76920_I2CVCC_PIN, HIGH);
            delayMicroseconds(900);
            i2c_power_enabled_ = true;
        }
    }
    inline void i2cPowerOff() {
        if (i2c_power_enabled_) {
            digitalWrite(BQ76920_I2CVCC_PIN, LOW);
            i2c_power_enabled_ = false;
        }
    }

    unsigned long last_balancing_stop_time_ = 0;  // 上次停止均衡的时间，0表示不在冷却期
    static const unsigned long BALANCE_COUNT_INTERVAL = 600000;

    // 自消耗计算 - 原始采样缓冲区
    RawSample raw_buffer_[RAW_BUFFER_SIZE];
    uint8_t raw_buffer_idx_ = 0;
    uint32_t last_raw_sample_time_ = 0;

    // 采集统计
    uint32_t raw_call_count_ = 0;
    uint32_t raw_skip_ntp_ = 0;
    uint32_t raw_skip_interval_ = 0;
    uint32_t raw_flush_count_ = 0;
    uint32_t raw_flush_fail_ = 0;
    float self_consumption_mA_ = 0.0f;       // 当前自消耗值 (0=未计算)
    uint8_t sc_segment_count_ = 0;            // 合格段数
    uint8_t sc_total_segments_ = 0;           // 总候选段数
    uint8_t sc_confidence_ = 0;               // 置信度
    uint32_t sc_last_update_ = 0;             // 最后计算时间
    uint32_t sc_last_check_ = 0;              // 最后检查时间

    void collectRawSample(const BMS_State& bmsState);
    void flushRawBuffer();
    void cleanupOldRawFiles();
    void loadSelfConsumption();
    void saveSelfConsumption();
    void loadIRData();
    void saveIRData();

    // 内阻估算结果（由 SystemManager 分析任务写入）
    float ir_result_mΩ_[5] = {};                // 每节电池内阻 (mΩ), 0=未测量
    uint8_t ir_sample_count_ = 0;               // 有效突变采样数
};

#endif