#ifndef JS_TEMPLATES_H
#define JS_TEMPLATES_H

#include <Arduino.h>

// =============================================================================
// SPA 页面 JavaScript
// =============================================================================
const char SPA_PAGE_JS[] PROGMEM = R"rawliteral(
var ws,rc=0;
var curPowerMode=-1;
var iwCfg={ssid:'',pass:'',ipMode:'dhcp',staticIp:'',staticGateway:'',staticSubnet:'',staticDns:''};

// === 多语言支持 ===
var LANG_DATA={
zh:{
pageTitle:"UPS 控制中心",connecting:"连接中",connected:"已连接",disconnected:"断开",
realtime:"实时监控",navStatus:"📊 状态概览",navBms:"🔋 BMS 状态",navPower:"⚡ 电源状态",navConfig:"⚙️ 系统配置",navOta:"📦 固件升级",btnRestart:"重启设备",btnLogout:"退出登录",jsConfirmLogout:"确定要退出登录吗？",
cardBattery:"🔋 电池状态",cardCells:"📊 单体电压",cardPower:"⚡ 电源状态",cardSystem:"🖥️ 系统状态",
lblVoltage:"电压",lblCurrent:"电流",lblTemp:"温度",lblHealth:"健康度",lblCycles:"循环次数",lblCapacity:"剩余容量",
lblMaxMin:"最高 / 最低",lblDiff:"压差",lblBalance:"均衡状态",lblBalTotal:"总均衡次数",
lblAc:"AC 电源",lblInVoltage:"输入电压",lblInCurrent:"输入电流",lblOutPower:"输出功率",
lblPmVoltage:"电池电压 (PM)",lblPmCurrent:"放电电流 (PM)",lblChargeStatus:"充电状态",
lblPowerMode:"运行模式",lblBoardTemp:"板温",lblEnvTemp:"环境温度",lblShtTemp:"SHTC3温度",lblShtHumid:"SHTC3湿度",
badgeOnline:"在线",badgeOffline:"离线",badgeCharging:"充电中",badgeNotCharging:"未充电",
badgeBalancing:"均衡中",badgeInactive:"未激活",badgeYes:"是",badgeNo:"否",badgeTriggered:"触发",badgeNormal:"正常",
powerModes:["AC","电池","混合","充电"],powerUnknown:"未知",
cardVoltageCurrent:"🔌 电压 & 电流",cardProtection:"🛡️ 保护 & 均衡",cardSelfConsumption:"📈 系统自消耗（实验性功能）",
lblTotalVoltage:"总电压",lblFaultType:"故障类型",lblBmsMode:"BMS 模式",lblScCurrent:"消耗电流",lblLastUpdate:"最后更新",
cardCellDetail:"📊 单体电压详情",lblMax:"最高",lblMin:"最低",lblAvg:"平均",lblIrSample:"内阻采样（实验性功能）",
cardBq76920Regs:"📋 BQ76920 寄存器状态",cardRawFiles:"📁 原始采样数据文件",cardLogFiles:"📁 日志文件",
rawFilesDesc:"每分钟采集一次，保存在 SPIFFS 分区 /raw/ 目录下，保留 30 天",logFilesDesc:"系统日志文件，保存在 SPIFFS 分区 /log/ 目录下，保留 7 天",btnRefreshRaw:"刷新采样文件列表",btnRefreshLog:"刷新日志文件列表",btnClickLoad:"点击上方按钮加载",
scCollecting:"采集中",scNotCalculated:"未计算",scPerDay:"mAh/天)",
cardInputPower:"🔌 输入电源",cardBatteryMon:"🔋 电池监测",cardChargeCtrl:"⚡ 充电控制",cardChipStatus:"💻 芯片状态",
lblAcStatus:"AC 状态",lblInputPower:"输入功率",lblChargeEnable:"充电使能",lblHybrid:"混合模式",
cfgLang:"🌐 语言 / Language",cfgLangLabel:"界面语言:",
cfgSystem:"⚙️ 系统设置",cfgHardware:"🔊 硬件控制",cfgBms:"🔋 BMS 配置",cfgWindows:"⏰ 充电窗口",cfgPower:"⚡ 电源管理",cfgCalibration:"📐 校准系数",cfgShipping:"📦 运输模式",
cfgChargeVoltage:"充电电压:",cfgDischargeSocStop:"停止 SOC:",
cfgWifi:"📶 WiFi 设置",cfgWifiName:"WiFi 名称:",cfgWifiPass:"WiFi 密码:",cfgIpMode:"IP 获取方式:",cfgDhcp:"动态获取 (DHCP)",cfgStaticIp:"固定 IP",
cfgStaticIpTitle:"🌐 静态 IP 配置",cfgIpAddress:"IP 地址:",cfgGateway:"网关:",cfgSubnet:"子网掩码:",cfgDns:"DNS 服务器:",
cfgNtp:"🕐 时间同步配置",cfgNtpServer:"NTP 服务器:",
cfgHid:"🎮 HID 配置",cfgHidService:"HID 服务:",cfgHidMode:"电量模式:",cfgMah:"毫安时 (mAh)",cfgMwh:"毫瓦时 (mWh)",cfgPct:"百分比 (%)（linux 选这个）",
cfgMqtt:"📡 MQTT 配置（需要重启后生效）",cfgMqttService:"MQTT 服务:",cfgMqttBroker:"Broker 地址:",cfgMqttPort:"端口:",cfgMqttUser:"用户名:",cfgMqttPass:"密码:",cfgOptional:"可选",
cfgEnabled:"启用",cfgBuzzer:"蜂鸣器:",cfgVolume:"音量:",cfgLedBrightness:"LED 亮度:",
cfgXiaomi:"📡 小米传感器桥接",cfgXiaomiDesc:"通过I2C从机模拟SHTC3传感器，供米家温湿度计2读取UPS数据。仅新版PCB支持。电池温度→温度，SOC→湿度。(配置后需要重启生效)",cfgSensorBridge:"传感器桥接:",
cfgCellCount:"电池串数:",cfgCells3:"3 串",cfgCells4:"4 串",cfgCells5:"5 串",cfgNominalCap:"标称容量:",
cfgOvThreshold:"过压阈值:",cfgUvThreshold:"欠压阈值:",cfgOvRecover:"过压恢复:",cfgUvRecover:"欠压恢复:",
cfgMaxCharge:"最大充电电流:",cfgMaxDischarge:"最大放电电流:",cfgShortCircuit:"短路阈值:",
cfgOverheatTemp:"关闭充放电温度:",cfgOverheatHint:"超过此温度将关闭充放电，需手动重启恢复",
cfgBattParams:"📋 电池参数",cfgVoltProtection:"⚡ 电压保护",cfgCurrProtection:"🔌 电流保护",cfgTempProtection:"🌡️ 温度保护",cfgBalConfig:"⚖️ 均衡配置",
cfgBalancing:"电池均衡:",cfgBalDiff:"均衡压差:",
cfgChemistry:"电池类型:",cfgChemNcm:"三元锂 (NCM)",cfgChemLfp:"磷酸铁锂 (LiFePO4)",
wzChemistry:"电池类型",
jsChemSwitchConfirm:"切换电池类型将重置全部电池学习数据(SOH/循环/库仑计)并重启设备。\n保护阈值将自动填充为推荐值，请确认后再保存。\n\n确定切换？",
jsChemRebooting:"电池类型已切换，设备正在重启...",
jsOvRange:"过压阈值超出所选电池类型的允许范围",
jsUvRange:"欠压阈值超出所选电池类型的允许范围",
cfgBalLfpTip:"磷酸铁锂电池仅在充电末端(单体>3.4V)执行均衡，平台区均衡无意义，属预期行为。",
cfgChargeWindow:"充电时间窗口",cfgWindowDesc:"配置允许充电的时间段。bit0=周日，bit1=周一 ... bit6=周六",cfgAddWindow:"添加窗口",cfgNoWindows:"暂无窗口",
cfgPowerMgmt:"电源管理",cfgChargeSocStart:"启动 SOC:",cfgChargeSocStop:"停止 SOC:",
cfgHybridPower:"混合供电:",cfgVsysMin:"最小系统电压:",cfgVsysMinHint:"仅 BQ24800 芯片有效，步进 256mV，默认 8960mV",
cfgProtection:"保护配置",cfgAcInputCurrent:"AC输入电流:",cfgAcInputHint:"芯片配置最大值为8064，超过此值都会限制到8064",cfgOverTempThreshold:"过温阈值:",
cfgChargeTempLimit:"🌡️ 充电温度限制",cfgChargeTempHigh:"充电最高温:",cfgChargeTempLow:"充电最低温:",
cfgChargeConfig:"🔌 充电配置",cfgDischargeConfig:"🔋 放电配置",cfgHybridSection:"🔄 混合供电",cfgProtConfig:"🛡️ 保护配置",
cfgCalDesc:"校正系数范围：50-255（表示 0.50x - 2.55x），100 = 1.00x（无校正）。修改后点击\"保存校准\"立即生效。",cfgSaveCal:"保存校准",
cfgBatteryReset:"🔄 电池数据重置",cfgBatteryResetDesc:"更换新电池后，重置 BMS 中记录的电池健康度(SOH)、循环次数、均衡统计数据。重置后系统将从开路电压重新初始化 SOC。",cfgResetBtn:"重置电池数据",
cfgShippingTitle:"📦 运输以及存储模式",cfgShippingDesc:"此模式会让bq76920电池管理芯片进入运输模式，也就是睡眠模式，将不会响应任何指令，一旦进入此模式，需要点按对应的硬件开关才能启用电池，此模式推荐运输或者长时间不使用情况下再执行。",cfgEnterMode:"进入此模式",
cfgSave:"保存配置",cfgSaveNote:"⚠️ 注意：",cfgSaveNoteText:"事关安全，保护参数请谨慎设置。",
otaDeviceInfo:"设备信息",otaChip:"芯片型号",otaFirmware:"当前固件",otaSpace:"可用空间",otaFlash:"Flash 大小",
otaNotes:"升级须知",otaNote1:"仅支持 .bin 格式固件文件",otaNote2:"升级过程中请勿断电或重启",otaNote3:"升级约需 1-2 分钟",otaNote4:"升级完成后设备自动重启",
otaFirmwareUpload:"固件上传",otaSelectFile:"点击选择固件文件或拖拽到此处",otaStartUpgrade:"开始升级",
wzTitle:"🔧 初始配置",wzSubtitle:"欢迎！请完成以下步骤来配置您的设备",
wzStepWifi:"WiFi",wzStepNetwork:"网络",wzStepBattery:"电池",wzStepHardware:"硬件",wzStepComplete:"完成",
wzWifiTitle:"📶 WiFi 设置",wzWifiDesc:"连接到您的家庭或办公室 WiFi 网络",wzWifiSsid:"WiFi 名称 (SSID)",wzWifiPass:"WiFi 密码",wzWifiSsidPh:"输入 WiFi 名称",wzWifiPassPh:"输入 WiFi 密码",
wzAuthTitle:"🔐 管理账户（必填）",wzAuthDesc:"用于登录设备管理页面，请妥善保管",wzAuthUser:"管理用户名",wzAuthPass:"管理密码（至少 8 位）",wzAuthPass2:"确认管理密码",
jsAuthUserRequired:"请输入管理用户名",jsAuthPassLen:"管理密码至少 8 位",jsAuthPassMismatch:"两次输入的密码不一致",
cfgAuth:"🔐 访问账户",cfgAuthDesc:"修改登录设备管理页面的用户名和密码，修改后需重新登录。",cfgAuthUser:"用户名:",cfgAuthPass:"新密码:",cfgAuthPass2:"确认密码:",cfgAuthSave:"修改账户",jsAuthChanged:"账户已修改，请重新登录",
wzNetworkTitle:"🌐 网络配置",wzIpMode:"IP 地址获取方式",wzDhcp:"动态获取 (DHCP)",wzStaticIp:"固定 IP 地址",wzIpAddress:"IP 地址",wzGateway:"网关",wzSubnet:"子网掩码",wzDns:"DNS 服务器",wzNtp:"NTP 时间服务器",
wzBatteryTitle:"🔋 电池配置",wzBatteryDesc:"请根据您的电池规格进行配置",wzCellCount:"电池串数 (Cells)",wzCells3:"3 串 (11.1V)",wzCells4:"4 串 (14.8V)",wzCells5:"5 串 (18.5V)",
wzCapacity:"电池容量",wzChargeCur:"最大充电电流 (mA)",wzDischargeCur:"最大放电电流 (mA)",wzOvThresh:"过压保护阈值 (mV)",wzUvThresh:"欠压保护阈值 (mV)",wzBalancing:"启用电池均衡",
wzHardwareTitle:"⚙️ 硬件设置",wzBuzzer:"启用蜂鸣器",wzBuzzerVol:"蜂鸣器音量",wzLedBrightness:"LED 亮度",wzHid:"启用 HID 报告",wzHidMode:"HID 电量模式",wzHidPct:"百分比 (%)(nas，linux选这个)",
wzCompleteTitle:"配置完成！",wzCompleteDesc:"配置已保存，设备即将重启",wzCompleteDesc2:"重启后请访问管理页面查看实时监控",
wzPrev:"上一步",wzNext:"下一步",wzSaveRestart:"保存并重启",
wzRegTitle:"寄存器状态",
jsDays:"天",jsHours:"时",jsMinutes:"分",jsSeconds:"秒",
jsRunning:"运行：",jsUpdating:"更新：",jsConfigMode:"配置模式",
jsWindow:"窗口 #",jsWeekday:"星期:",jsStart:"开始:",jsEnd:"结束:",jsDelete:"删除",jsFull:"已满 (5/5)",jsAddWindow:"添加窗口",
jsNoWindows:"暂无窗口",
jsSelectBin:"请选择 .bin 文件",jsReady:"就绪，点击升级",jsUploading:"上传中...",jsUpgradeSuccess:"升级成功，重启中...",jsUpgrading:"升级中...",jsRetry:"重新升级",jsNetworkError:"网络错误",
jsSaveNetChange:"网络变动，保存成功。设备即将重启...",jsSaveSuccess:"保存成功",jsSaveFailed:"保存失败：",jsNetworkErr:"网络错误",
jsEnterStaticIp:"请输入静态 IP",jsEnterCapacity:"请输入有效的电池容量",jsOvRange:"过压阈值必须在 4000-4500mV 之间",jsUvRange:"欠压阈值必须在 2500-3500mV 之间",
jsConfirmRestart:"确定要重启设备吗？\n\n设备将在重启后短暂离线。",jsNoAcRestart:"当前非 AC 供电模式，无法重启设备。\n\n请连接电源适配器后再尝试重启。",jsRestarting:"设备即将重启，请等待几秒后刷新页面。",
jsConfirmShip:"一旦进入此模式，需要点按对应的硬件开关才能启用电池\n\n确定要进入运输模式吗？",jsShipSuccess:"已进入运输模式！\n\nBMS 芯片已进入睡眠模式，需要点按硬件开关才能重新启用电池。",
jsConfirmReset:"确定要重置电池数据吗？\n\n此操作将重置以下数据：\n- 电池健康度 (SOH) → 100%\n- 循环次数 → 0\n- 均衡统计数据 → 0\n- 库仑计累积值\n- SOH 学习数据\n\n重置后系统将从开路电压重新初始化 SOC。",
jsLoading:"加载中...",jsNoData:"暂无数据文件",jsLoadFailed:"加载失败",jsCalSaved:"保存成功！",jsCalFailed:"保存失败：",jsCalRange:"系数范围 50-255",
jsRebooting:"重启中...",jsProcessing:"处理中...",jsResetSuccess:"重置成功！",jsResetFailed:"失败：",
jsIrCount:"次"
},
en:{
pageTitle:"UPS Control Center",connecting:"Connecting",connected:"Connected",disconnected:"Disconnected",
realtime:"Realtime",navStatus:"📊 Status",navBms:"🔋 BMS Status",navPower:"⚡ Power Status",navConfig:"⚙️ Configuration",navOta:"📦 Firmware Update",btnRestart:"Restart",btnLogout:"Log Out",jsConfirmLogout:"Log out?",
cardBattery:"🔋 Battery",cardCells:"📊 Cell Voltages",cardPower:"⚡ Power",cardSystem:"🖥️ System",
lblVoltage:"Voltage",lblCurrent:"Current",lblTemp:"Temperature",lblHealth:"Health",lblCycles:"Cycles",lblCapacity:"Remaining",
lblMaxMin:"Max / Min",lblDiff:"Delta",lblBalance:"Balancing",lblBalTotal:"Total Balancings",
lblAc:"AC Power",lblInVoltage:"Input Voltage",lblInCurrent:"Input Current",lblOutPower:"Output Power",
lblPmVoltage:"Battery Voltage (PM)",lblPmCurrent:"Discharge Current (PM)",lblChargeStatus:"Charge Status",
lblPowerMode:"Power Mode",lblBoardTemp:"Board Temp",lblEnvTemp:"Env Temp",lblShtTemp:"SHTC3 Temp",lblShtHumid:"SHTC3 Humidity",
badgeOnline:"Online",badgeOffline:"Offline",badgeCharging:"Charging",badgeNotCharging:"Not Charging",
badgeBalancing:"Balancing",badgeInactive:"Inactive",badgeYes:"Yes",badgeNo:"No",badgeTriggered:"Triggered",badgeNormal:"Normal",
powerModes:["AC","Battery","Hybrid","Charging"],powerUnknown:"Unknown",
cardVoltageCurrent:"🔌 Voltage & Current",cardProtection:"🛡️ Protection & Balancing",cardSelfConsumption:"📈 Self-Consumption (Experimental)",
lblTotalVoltage:"Total Voltage",lblFaultType:"Fault Type",lblBmsMode:"BMS Mode",lblScCurrent:"Consumption",lblLastUpdate:"Last Update",
cardCellDetail:"📊 Cell Voltage Details",lblMax:"Max",lblMin:"Min",lblAvg:"Avg",lblIrSample:"IR Sample (Exp.)",
cardBq76920Regs:"📋 BQ76920 Registers",cardRawFiles:"📁 Raw Sample Files",cardLogFiles:"📁 Log Files",
rawFilesDesc:"Collected every minute, stored in SPIFFS /raw/, retained for 30 days",logFilesDesc:"System log files, stored in SPIFFS /log/, retained for 7 days",btnRefreshRaw:"Refresh Raw Files",btnRefreshLog:"Refresh Log Files",btnClickLoad:"Click above to load",
scCollecting:"Collecting",scNotCalculated:"Not calculated",scPerDay:"mAh/day)",
cardInputPower:"🔌 Input Power",cardBatteryMon:"🔋 Battery Monitor",cardChargeCtrl:"⚡ Charge Control",cardChipStatus:"💻 Chip Status",
lblAcStatus:"AC Status",lblInputPower:"Input Power",lblChargeEnable:"Charge Enable",lblHybrid:"Hybrid Mode",
cfgLang:"🌐 Language",cfgLangLabel:"Language:",
cfgSystem:"⚙️ System",cfgHardware:"🔊 Hardware",cfgBms:"🔋 BMS Config",cfgWindows:"⏰ Charge Windows",cfgPower:"⚡ Power Mgmt",cfgCalibration:"📐 Calibration",cfgShipping:"📦 Shipping Mode",
cfgChargeVoltage:"Charge Voltage:",cfgDischargeSocStop:"Stop SOC:",
cfgWifi:"📶 WiFi Settings",cfgWifiName:"WiFi Name:",cfgWifiPass:"WiFi Password:",cfgIpMode:"IP Mode:",cfgDhcp:"DHCP",cfgStaticIp:"Static IP",
cfgStaticIpTitle:"🌐 Static IP Config",cfgIpAddress:"IP Address:",cfgGateway:"Gateway:",cfgSubnet:"Subnet:",cfgDns:"DNS Server:",
cfgNtp:"🕐 Time Sync",cfgNtpServer:"NTP Server:",
cfgHid:"🎮 HID Config",cfgHidService:"HID Service:",cfgHidMode:"Capacity Mode:",cfgMah:"mAh",cfgMwh:"mWh",cfgPct:"Percentage (%) (for Linux)",
cfgMqtt:"📡 MQTT Config (restart required)",cfgMqttService:"MQTT Service:",cfgMqttBroker:"Broker:",cfgMqttPort:"Port:",cfgMqttUser:"Username:",cfgMqttPass:"Password:",cfgOptional:"Optional",
cfgEnabled:"Enabled",cfgBuzzer:"Buzzer:",cfgVolume:"Volume:",cfgLedBrightness:"LED Brightness:",
cfgXiaomi:"📡 Xiaomi Sensor Bridge",cfgXiaomiDesc:"Simulates SHTC3 via I2C slave for Mi Temperature/Humidity Monitor 2. New PCB only. Battery temp→Temp, SOC→Humidity. (Restart required)",cfgSensorBridge:"Sensor Bridge:",
cfgCellCount:"Cells:",cfgCells3:"3S",cfgCells4:"4S",cfgCells5:"5S",cfgNominalCap:"Capacity:",
cfgOvThreshold:"OV Threshold:",cfgUvThreshold:"UV Threshold:",cfgOvRecover:"OV Recover:",cfgUvRecover:"UV Recover:",
cfgMaxCharge:"Max Charge Current:",cfgMaxDischarge:"Max Discharge Current:",cfgShortCircuit:"Short Circuit:",
cfgOverheatTemp:"Overheat Temp:",cfgOverheatHint:"Disables charge/discharge above this temp, manual restart required",
cfgBattParams:"📋 Battery Params",cfgVoltProtection:"⚡ Voltage Protection",cfgCurrProtection:"🔌 Current Protection",cfgTempProtection:"🌡️ Temperature Protection",cfgBalConfig:"⚖️ Balancing Config",
cfgBalancing:"Balancing:",cfgBalDiff:"Balancing Diff:",
cfgChemistry:"Battery Chemistry:",cfgChemNcm:"Li-ion (NCM)",cfgChemLfp:"LiFePO4",
wzChemistry:"Battery Chemistry",
jsChemSwitchConfirm:"Switching battery chemistry will reset all battery learning data (SOH/cycles/coulomb counter) and reboot the device.\nProtection thresholds will be auto-filled with recommended values.\n\nConfirm switch?",
jsChemRebooting:"Battery chemistry changed, device is rebooting...",
jsOvRange:"OV threshold out of range for selected chemistry",
jsUvRange:"UV threshold out of range for selected chemistry",
cfgBalLfpTip:"LiFePO4 cells only balance at charge tail (cell>3.4V). No balancing in flat region is expected.",
cfgChargeWindow:"Charge Time Windows",cfgWindowDesc:"Configure allowed charging periods. bit0=Sun, bit1=Mon ... bit6=Sat",cfgAddWindow:"Add Window",cfgNoWindows:"No windows",
cfgPowerMgmt:"Power Management",cfgChargeSocStart:"Start SOC:",cfgChargeSocStop:"Stop SOC:",
cfgHybridPower:"Hybrid Power:",cfgVsysMin:"Min System Voltage:",cfgVsysMinHint:"BQ24800 only, step 256mV, default 8960mV",
cfgProtection:"Protection",cfgAcInputCurrent:"AC Input Current:",cfgAcInputHint:"Max chip config is 8064, values above will be clamped",cfgOverTempThreshold:"Over Temp Threshold:",
cfgChargeTempLimit:"🌡️ Charge Temp Limits",cfgChargeTempHigh:"Max Charge Temp:",cfgChargeTempLow:"Min Charge Temp:",
cfgChargeConfig:"🔌 Charge Config",cfgDischargeConfig:"🔋 Discharge Config",cfgHybridSection:"🔄 Hybrid Power",cfgProtConfig:"🛡️ Protection Config",
cfgCalDesc:"Range: 50-255 (0.50x - 2.55x), 100 = 1.00x (no correction). Click \"Save Calibration\" to apply.",cfgSaveCal:"Save Calibration",
cfgBatteryReset:"🔄 Battery Data Reset",cfgBatteryResetDesc:"Reset SOH, cycle count, balancing stats in BMS after battery replacement. SOC will be re-initialized from OCV.",cfgResetBtn:"Reset Battery Data",
cfgShippingTitle:"📦 Shipping & Storage Mode",cfgShippingDesc:"Puts BQ76920 into ship mode (sleep). Won't respond to any commands. Hardware button required to re-enable. Use for shipping or long-term storage.",cfgEnterMode:"Enter Mode",
cfgSave:"Save Config",cfgSaveNote:"⚠️ Note:",cfgSaveNoteText:"Safety-critical parameters, please set carefully.",
otaDeviceInfo:"Device Info",otaChip:"Chip",otaFirmware:"Firmware",otaSpace:"Free Space",otaFlash:"Flash Size",
otaNotes:"Upgrade Notes",otaNote1:".bin files only",otaNote2:"Do not power off during upgrade",otaNote3:"Upgrade takes 1-2 minutes",otaNote4:"Device auto-restarts after upgrade",
otaFirmwareUpload:"Firmware Upload",otaSelectFile:"Click to select or drag firmware here",otaStartUpgrade:"Start Upgrade",
wzTitle:"🔧 Initial Setup",wzSubtitle:"Welcome! Please complete the following steps to configure your device",
wzStepWifi:"WiFi",wzStepNetwork:"Network",wzStepBattery:"Battery",wzStepHardware:"Hardware",wzStepComplete:"Done",
wzWifiTitle:"📶 WiFi Setup",wzWifiDesc:"Connect to your home or office WiFi network",wzWifiSsid:"WiFi Name (SSID)",wzWifiPass:"WiFi Password",wzWifiSsidPh:"Enter WiFi name",wzWifiPassPh:"Enter WiFi password",
wzAuthTitle:"🔐 Admin Account (Required)",wzAuthDesc:"Used to log in to the device management page",wzAuthUser:"Admin Username",wzAuthPass:"Admin Password (min 8 chars)",wzAuthPass2:"Confirm Password",
jsAuthUserRequired:"Admin username required",jsAuthPassLen:"Admin password must be at least 8 chars",jsAuthPassMismatch:"Passwords do not match",
cfgAuth:"🔐 Access Account",cfgAuthDesc:"Change the username/password for this device. Re-login required after change.",cfgAuthUser:"Username:",cfgAuthPass:"New Password:",cfgAuthPass2:"Confirm:",cfgAuthSave:"Change Account",jsAuthChanged:"Account changed, please re-login",
wzNetworkTitle:"🌐 Network Config",wzIpMode:"IP Address Mode",wzDhcp:"DHCP",wzStaticIp:"Static IP",wzIpAddress:"IP Address",wzGateway:"Gateway",wzSubnet:"Subnet",wzDns:"DNS Server",wzNtp:"NTP Server",
wzBatteryTitle:"🔋 Battery Config",wzBatteryDesc:"Configure according to your battery specs",wzCellCount:"Cells",wzCells3:"3S (11.1V)",wzCells4:"4S (14.8V)",wzCells5:"5S (18.5V)",
wzCapacity:"Capacity",wzChargeCur:"Max Charge Current (mA)",wzDischargeCur:"Max Discharge Current (mA)",wzOvThresh:"OV Threshold (mV)",wzUvThresh:"UV Threshold (mV)",wzBalancing:"Enable Balancing",
wzHardwareTitle:"⚙️ Hardware",wzBuzzer:"Enable Buzzer",wzBuzzerVol:"Buzzer Volume",wzLedBrightness:"LED Brightness",wzHid:"Enable HID Report",wzHidMode:"HID Capacity Mode",wzHidPct:"Percentage (%) (for Linux/NAS)",
wzCompleteTitle:"Setup Complete!",wzCompleteDesc:"Config saved, device will restart",wzCompleteDesc2:"Visit the dashboard after restart to monitor in realtime",
wzPrev:"Previous",wzNext:"Next",wzSaveRestart:"Save & Restart",
wzRegTitle:"Registers",
jsDays:"d",jsHours:"h",jsMinutes:"m",jsSeconds:"s",
jsRunning:"Uptime: ",jsUpdating:"Updated: ",jsConfigMode:"Config Mode",
jsWindow:"Window #",jsWeekday:"Week:",jsStart:"Start:",jsEnd:"End:",jsDelete:"Delete",jsFull:"Full (5/5)",jsAddWindow:"Add Window",
jsNoWindows:"No windows",
jsSelectBin:"Please select a .bin file",jsReady:"Ready, click to upgrade",jsUploading:"Uploading...",jsUpgradeSuccess:"Upgrade successful, restarting...",jsUpgrading:"Upgrading...",jsRetry:"Retry",jsNetworkError:"Network error",
jsSaveNetChange:"Network changed, saved. Device will restart...",jsSaveSuccess:"Saved",jsSaveFailed:"Save failed: ",jsNetworkErr:"Network error",
jsEnterStaticIp:"Please enter static IP",jsEnterCapacity:"Please enter valid battery capacity",jsOvRange:"OV threshold must be 4000-4500mV",jsUvRange:"UV threshold must be 2500-3500mV",
jsConfirmRestart:"Restart device?\n\nDevice will be briefly offline.",jsNoAcRestart:"Cannot restart: not on AC power.\n\nPlease connect the power adapter first.",jsRestarting:"Device restarting, please wait and refresh.",
jsConfirmShip:"Entering ship mode requires a hardware button press to re-enable the battery.\n\nAre you sure?",jsShipSuccess:"Ship mode entered!\n\nBMS chip is now sleeping. Hardware button required to re-enable.",
jsConfirmReset:"Reset battery data?\n\nThis will reset:\n- SOH → 100%\n- Cycle count → 0\n- Balancing stats → 0\n- Coulomb counter\n- SOH learning data\n\nSOC will be re-initialized from OCV.",
jsLoading:"Loading...",jsNoData:"No data files",jsLoadFailed:"Load failed",jsCalSaved:"Saved!",jsCalFailed:"Failed: ",jsCalRange:"Range 50-255",
jsRebooting:"Restarting...",jsProcessing:"Processing...",jsResetSuccess:"Reset successful!",jsResetFailed:"Failed: ",
jsIrCount:" times"
}
};
var L=LANG_DATA[window.CURLANG||'zh'];

function applyLang(lang){
window.CURLANG=lang;L=LANG_DATA[lang];
document.querySelectorAll('[data-i18n]').forEach(function(el){
var key=el.getAttribute('data-i18n');
if(L[key]!==undefined){
if(el.tagName==='INPUT'&&(el.type==='text'||el.type==='password'||el.type==='number'))el.placeholder=L[key];
else if(el.tagName==='OPTION')el.textContent=L[key];
else el.textContent=L[key];
}
});
if($('wz-chemistry')&&typeof wzChemChange==='function')wzChemChange();
document.title=L.pageTitle;
document.documentElement.lang=lang;
var ws=$('wsSt');if(ws){ws.textContent=ws.className.indexOf('ok')>=0?L.connected:L.disconnected}
weekdayNames=lang==='en'?['Sun','Mon','Tue','Wed','Thu','Fri','Sat']:['周日','周一','周二','周三','周四','周五','周六'];
calDescs=lang==='en'?['Input current sense','Discharge current sense','Power monitor','Input voltage','Battery voltage','Board temp (NTC)','Env temp (NTC)']:['输入电流检测','放电电流检测','系统功率监测','输入电压','电池电压','主板温度 (NTC)','环境温度 (NTC)'];
var ls=$('langSel');if(ls)ls.value=lang;var wl=$('wz-lang');if(wl)wl.value=lang;
renW();
}
function switchLang(lang){
applyLang(lang);
fetch('/api/set-lang',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({lang:lang})}).catch(function(){});
}

document.addEventListener('DOMContentLoaded',function(){iwCfg={ssid:$('ws').value,pass:$('wp').value,ipMode:$('ipMode').value,staticIp:$('sip').value,staticGateway:$('sgw').value,staticSubnet:$('ssn').value,staticDns:$('sdns').value};var cl=window.CURLANG||'zh';applyLang(cl);var ls=$('langSel');if(ls)ls.value=cl;var wl=$('wz-lang');if(wl)wl.value=cl});

// === 化学类型边界表（与 bms.cpp CHEM_LIMITS_* 保持一致） ===
var CHEM={ncm:{ovMin:4000,ovMax:4500,uvMin:2500,uvMax:3500,ovrMin:4000,ovrMax:4300,uvrMin:2800,uvrMax:3300,recOv:4210,recUv:3000,recOvR:4180,recUvR:3050,recCellV:4150,cellV:[11.1,14.8,18.5]},
lifepo4:{ovMin:3500,ovMax:3800,uvMin:2000,uvMax:2900,ovrMin:3400,ovrMax:3600,uvrMin:2300,uvrMax:3000,recOv:3650,recUv:2500,recOvR:3550,recUvR:2800,recCellV:3600,cellV:[9.6,12.8,16.0]}};

function setChemBounds(t){
var c=CHEM[t]||CHEM.ncm;
function sb(id,mn,mx){var e=$(id);if(e){e.min=mn;e.max=mx}}
sb('bo',c.ovMin,c.ovMax);sb('bu',c.uvMin,c.uvMax);sb('bor',c.ovrMin,c.ovrMax);sb('bur',c.uvrMin,c.uvrMax);
}
document.addEventListener('DOMContentLoaded',function(){var s=$('bchem');if(s)setChemBounds(s.value)});

function onChemChange(){
var sel=$('bchem'),t=sel.value;
if(!confirm(L.jsChemSwitchConfirm)){sel.value=t==='ncm'?'lifepo4':'ncm';return}
var c=CHEM[t];
$('bo').value=c.recOv;$('bu').value=c.recUv;$('bor').value=c.recOvR;$('bur').value=c.recUvR;
$('pcv').value=$('bc').value*c.recCellV;
setChemBounds(t);
}
function wzChemChange(){
var t=$('wz-chemistry').value,c=CHEM[t];
var cc=$('wz-cell-count'),cv=c.cellV;
for(var i=0;i<cc.options.length;i++){var k=L['wzCells'+(i+3)]||cc.options[i].textContent;cc.options[i].textContent=k.replace(/[\d.]+V/,cv[i]+'V')}
var ov=$('wz-ov-threshold'),uv=$('wz-uv-threshold');
if(ov){ov.min=c.ovMin;ov.max=c.ovMax;ov.placeholder=c.recOv}
if(uv){uv.min=c.uvMin;uv.max=c.uvMax;uv.placeholder=c.recUv}
}

// === 面板切换 ===
function show(n,el){
document.querySelectorAll('.pnl').forEach(function(p){p.classList.remove('active')});
document.querySelectorAll('.side .si').forEach(function(s){s.classList.remove('active')});
document.getElementById('p-'+n).classList.add('active');
if(el)el.classList.add('active');
if(n==='config'){
var first=document.getElementById('p-cfg-system');if(first)first.classList.add('active');
var tabs=document.querySelectorAll('#p-config .si');if(tabs.length){tabs.forEach(function(t){t.classList.remove('active')});tabs[0].classList.add('active')}
var bar=$('cfgSaveBar');if(bar)bar.style.display='';
var note=$('cfgSaveNote');if(note)note.style.display='';
}
}
function showCfg(n,el){
document.querySelectorAll('#p-config .pnl').forEach(function(p){p.classList.remove('active')});
document.querySelectorAll('#p-config .si').forEach(function(s){s.classList.remove('active')});
document.getElementById('p-cfg-'+n).classList.add('active');
if(el)el.classList.add('active');
var hide=n==='calibration'||n==='shipping'||n==='auth';
var bar=$('cfgSaveBar');if(bar)bar.style.display=hide?'none':'';
var note=$('cfgSaveNote');if(note)note.style.display=hide?'none':'';
if(n==='calibration')loadCalibration();
}
// === WebSocket ===
function conn(){
ws=new WebSocket('ws://'+location.host+'/ws');
ws.onopen=function(){document.getElementById('wsSt').className='ws-st ok';document.getElementById('wsSt').textContent=L.connected;rc=0};
ws.onclose=function(){document.getElementById('wsSt').className='ws-st fail';document.getElementById('wsSt').textContent=L.disconnected;setTimeout(conn,Math.min(1e3*Math.pow(2,rc++),3e4))};
ws.onmessage=function(e){upd(JSON.parse(e.data))};
}
function badge(c,t){return'<span class="badge '+c+'">'+t+'</span>'}
function renderCells(cs,max,min,balCounts,irArr){var h='';for(var i=0;i<5;i++){var v=cs[i]||0;var bc=balCounts?balCounts[i]:0;var ir=irArr?irArr[i]:0;h+='<div class="cell"><div class="cn">C'+(i+1)+'</div><div class="cv" style="color:'+(v===max?'#52c41a':v===min?'#f5222d':'#333')+'">'+v+'/'+bc+'</div>'+(ir>0?'<div class="ir">'+ir.toFixed(1)+' mΩ</div>':'')+'</div>'}return h}
function getBalancingCells(mask){if(!mask)return[];var cells=[];for(var i=0;i<5;i++){if(mask&(1<<i))cells.push('Cell'+(i+1))}return cells}
function setStat(id,val,c){var e=$(id);e.textContent=val+' mV';e.className='stat-value'+c}
// === 数据刷新 ===
var $=function(id){return document.getElementById(id)}
function upd(d){
if(d.status==='config_mode'){$('hSt').textContent=L.jsConfigMode;return}
var b=d.bms||{},p=d.power||{},s=d.system||{};
curPowerMode=d.power_mode;
$('uptime').textContent=L.jsRunning+fmtUp(s.uptime||0);
$('wifi').textContent=s.wifi_ssid||'--';
$('rssi').textContent=(s.wifi_rssi||0)+' dBm';
var sc=b.soc||0;
$('soc').innerHTML=sc.toFixed(1)+'<span class="bigu">%</span>';
var bar=$('socBar');bar.style.width=sc+'%';bar.textContent=sc.toFixed(1)+'%';bar.style.background=sc>20?'#52c41a':sc>10?'#faad14':'#f5222d';
$('battV').textContent=(b.voltage||0)+' mV';
var ci=b.current||0;$('battI').textContent=ci+' mA';$('battI').className='vl'+(ci>0?' g':ci<0?' w':'');
$('battT').textContent=(b.temperature||0).toFixed(1)+' °C';$('soh').textContent=(b.soh||0).toFixed(1)+' %';
$('cycles').textContent=b.cycle_count||0;$('capR').textContent=(b.capacity_remaining||0)+' mAh';
var cs=b.cell_voltages||[],bcs=b.cell_balancing_count||[],irs=b.cell_ir||[];$('cells').innerHTML=renderCells(cs,b.cell_voltage_max||0,b.cell_voltage_min||0,bcs,irs);
$('cellMM').textContent=(b.cell_voltage_max||0)+'/'+(b.cell_voltage_min||0)+' mV';
$('cellD').textContent=((b.cell_voltage_max||0)-(b.cell_voltage_min||0))+' mV';
// 更新均衡状态显示
if(b.balancing_active&&b.balance_mask){var balCells=getBalancingCells(b.balance_mask);$('balSt').innerHTML=badge('g',L.badgeBalancing+': '+balCells.join(','))}else{$('balSt').innerHTML=badge('b',L.badgeInactive)}
// 更新总均衡次数
$('balTotal').textContent=b.balancing_events_total||0;
$('acSt').innerHTML=p.ac_present?badge('g',L.badgeOnline):badge('r',L.badgeOffline);
$('inV').textContent=(p.input_voltage||0)+' mV';$('inI').textContent=(p.input_current||0)+' mA';
$('outP').textContent=(p.output_power||0)+' W';
$('pmBattV').textContent=(p.battery_voltage||0)+' mV';$('pmBattI').textContent=(p.battery_current||0)+' mA';
$('chgSt').innerHTML=p.charger_enabled?badge('g',L.badgeCharging):badge('b',L.badgeNotCharging);
$('pwrMd').innerHTML=badge('b',L.powerModes[d.power_mode]||L.powerUnknown);
$('brdT').textContent=(s.board_temperature||0).toFixed(1)+' °C';$('envT').textContent=(s.environment_temperature||0).toFixed(1)+' °C';$('shtT').textContent=(s.board_temperature_sht||0).toFixed(1)+' °C';$('shtH').textContent=(s.board_humidity||0).toFixed(1)+' %';
// BMS panel
var bsc=b.soc||0,bb=$('b_soc_bar');$('b_soc').textContent=bsc.toFixed(1)+' %';bb.style.width=bsc+'%';bb.textContent=bsc.toFixed(1)+'%';bb.style.background=bsc>20?'#52c41a':bsc>10?'#faad14':'#f5222d';
$('b_soh').textContent=(b.soh||0).toFixed(1)+' %';$('b_v').textContent=(b.voltage||0)+' mV';
var bci=b.current||0;$('b_i').textContent=bci+' mA';$('b_i').className='vl'+(bci>0?' g':bci<0?' w':'');
$('b_t').textContent=(b.temperature||0).toFixed(1)+' °C';$('b_cyc').textContent=b.cycle_count||0;$('b_cap').textContent=(b.capacity_remaining||0)+' mAh';
$('b_bal').innerHTML=b.balancing_active?badge('g',L.badgeBalancing):badge('b',L.badgeInactive);
// 更新BMS面板的总均衡次数
$('b_bal_total').textContent=b.balancing_events_total||0;
$('b_fault').textContent=b.fault_type||0;$('b_mode').textContent=b.bms_mode||0;
var bcs2=b.cell_balancing_count||[];$('b_cells').innerHTML=renderCells(cs,b.cell_voltage_max||0,b.cell_voltage_min||0,bcs2,irs);
setStat('b_max',b.cell_voltage_max||0,' g');setStat('b_min',b.cell_voltage_min||0,' r');setStat('b_avg',b.cell_voltage_avg||0,' b');
setStat('b_dlt',(b.cell_voltage_max||0)-(b.cell_voltage_min||0),' w');
$('b_ir_cnt').textContent=(b.ir_sample_count||0)+L.jsIrCount;
// Power panel
$('p_ac').innerHTML=p.ac_present?badge('g',L.badgeOnline):badge('r',L.badgeOffline);$('p_iv').textContent=(p.input_voltage||0)+' mV';$('p_ii').textContent=(p.input_current||0)+' mA';
$('p_ip').textContent=((p.input_voltage||0)*(p.input_current||0)/1e6).toFixed(2)+' W';$('p_op').textContent=(p.output_power||0)+' W';
$('p_bv').textContent=(p.battery_voltage||0)+' mV';$('p_bi').textContent=(p.battery_current||0)+' mA';
$('p_ce').innerHTML=p.charger_enabled?badge('g',L.badgeYes):badge('b',L.badgeNo);$('p_hy').innerHTML=p.hybrid_mode?badge('g',L.badgeYes):badge('b',L.badgeNo);
$('p_ft').textContent=p.fault_type||0;$('p_ph').innerHTML=!p.prochot_status?badge('r',L.badgeTriggered):badge('g',L.badgeNormal);$('p_tb').innerHTML=!p.tbstat_status?badge('w',L.badgeTriggered):badge('g',L.badgeNormal);
// Self-consumption
var sc_ma=d.self_consumption_mA||0;
$('sc_mA').textContent=sc_ma>0?sc_ma.toFixed(1)+' mA ('+(sc_ma*24).toFixed(0)+L.scPerDay:L.scCollecting;
var sc_ts=d.sc_last_update||0;
var sc_check=d.sc_last_check||0;
if(sc_ts>0){$('sc_time').textContent=new Date(sc_ts*1000).toLocaleDateString()}
else if(sc_check>0){$('sc_time').textContent=new Date(sc_check*1000).toLocaleDateString()+' ('+L.scCollecting+')'}
else{$('sc_time').textContent=L.scNotCalculated}
$('updT').textContent=L.jsUpdating+new Date().toLocaleTimeString();
// Tips
var tips=d.tips||[];var tb=$('tipBar');
if(tips.length>0){var th='';for(var ti=0;ti<tips.length;ti++){var t=tips[ti];th+='<div class="tip-item"><span class="tip-time">'+(t.msg||'')+'</span></div>'}tb.innerHTML='<div class="tip-list">'+th+'</div><button class="tip-close" onclick="clearTips()">&times;</button>';tb.classList.add('has-tips')}else{tb.innerHTML='';tb.classList.remove('has-tips')}
// Regs
var r2=p.bq24780s_registers||[],r7=b.bq76920_registers||[];
var cv=p.chip_variant||0;$('regTitle').textContent=(cv===1?'BQ24800':'BQ24780S')+' '+L.wzRegTitle;
var a2=['0x12','0x3B','0x38','0x37','0x3C','0x3D','0x3A','0x14','0x15','0x39','0x3F'];
var n2=['CHARGE_OPTION0','CHARGE_OPTION1','CHARGE_OPTION2','CHARGE_OPTION3','PROCHOT_OPTION0','PROCHOT_OPTION1','PROCHOT_STATUS','CHARGE_CURRENT','CHARGE_VOLTAGE','DISCHARGE_CURRENT','INPUT_CURRENT'];
if(cv===1){a2.push('0x3E');n2.push('VSYS_MIN')}
var rc2=cv===1?12:11;
var h2='';for(var i=0;i<rc2;i++){var v=r2[i]||0;h2+='<div class="card"><div class="card-t" style="font-size:13px">'+a2[i]+' '+n2[i]+'</div><div style="color:#1677ff;font-weight:700;font-size:16px">0x'+v.toString(16).toUpperCase().padStart(4,'0')+'</div>'+pB2(i,v,cv)+'</div>'}
$('r24').innerHTML=h2;
var a7=['0x00','0x01','0x04','0x05','0x06','0x07','0x08','0x09','0x0A','0x0B','0x50','0x51'];
var n7=['SYS_STAT','CELLBAL1','SYS_CTRL1','SYS_CTRL2','PROTECT1','PROTECT2','PROTECT3','OV_TRIP','UV_TRIP','CC_CFG','GAIN_uV','OFFSET_mV'];
var h7='';for(var i=0;i<12;i++){var v=r7[i]||0,bt='',hx='0x'+v.toString(16).toUpperCase().padStart(2,'0');for(var j=7;j>=0;j--)bt+=(v>>j&1);h7+='<div class="card"><div class="card-t" style="font-size:13px">'+a7[i]+' '+n7[i]+'</div><div style="color:#1677ff;font-weight:700;font-size:16px">'+hx+' <span style="color:#bbb;font-size:10px;font-family:monospace">'+bt+'</span></div>'+pB7(i,v,r7)+'</div>'}
$('r76').innerHTML=h7;
}
function pB2(i,v,cv){
var r='',st='<div style="font-size:11px;',sb=st+'color:#888">',sc=st+'color:#52c41a;font-weight:600">';
switch(i){
case 0:r+=sc+'模式：'+(v>>15&1?'低功耗':'性能')+'</div>'+sb+'看门狗：'+['禁用','5s','88s','175s'][(v>>13)&3]+'</div>'+sb+'PWM: '+['600kHz','800kHz','1MHz','-'][(v>>8)&3]+'</div>'+sb+'LEARN: '+(v>>5&1?'使能':'禁用')+'</div>'+sb+'IADP 增益：'+(v>>4&1?'40x':'20x')+'</div>'+sb+'IDCHG 增益：'+(v>>3&1?'16x':'8x')+'</div>'+st+'color:#888">充电：'+(v&1?'抑制':'使能')+'</div>';break;
case 1:r+=sb+'欠压阈值：'+['59.19%','62.65%','66.55%','70.97%'][(v>>14)&3]+'</div>'+sb+'IDCHG: '+(v>>11&1?'使能':'禁用')+'</div>'+sb+'PMON: '+(v>>10&1?'使能':'禁用')+'</div>';break;
case 2:r+=sb+'外部 ILIM: '+(v>>7&1?'使能':'禁用')+'</div>';if(cv===1){r+=sb+'电池升压：'+(v>>6&1?'使能':'禁用')+'</div>'+sb+'升压电压：'+(v>>5&1?'VSYSMIN+2.3V':'VSYSMIN+1.5V')+'</div>'}break;
case 3:r+=sb+'放电调节：'+(v>>15&1?'使能':'禁用')+'</div>'+sb+'ACOK 去抖：'+(v>>12&1?'1.3s':'150ms')+'</div>'+sb+'AC 存在：'+(v>>11&1?'是':'否')+'</div>';break;
case 4:var ic=(v>>11)&31;r+=sb+'ICRIT: '+(ic>26?'溢出':(110+ic*5)+'%')+'</div>';break;
case 5:r+=sb+'IDCHG 阈值：'+((v>>10)&63)*512+'mA</div>';break;
case 6:var nm=['ACOK','BATPRES','VSYS','IDCHG','INOM','ICRIT','CMP'];r+=st+'color:#888">PROCHOT:</div>';for(var j=0;j<7;j++)r+=st+'color:'+(v>>j&1?'#f5222d':'#52c41a')+'">'+nm[j]+': '+(v>>j&1?'触发':'正常')+'</div>';break;
case 7:r+=sc+'充电电流：'+((v>>6)&127)*64+'mA</div>';break;
case 8:r+=sc+'充电电压：'+((v>>4)&1023)*16+'mV</div>';break;
case 9:r+=sc+'放电电流：'+((v>>9)&63)*512+'mA</div>';break;
case 10:r+=sc+'输入电流：'+((v>>7)&63)*128+'mA</div>';break;
case 11:r+=sc+'最小系统电压：'+((v>>8)&63)*256+'mV</div>'+sb+'(仅 BQ24800)</div>';break;
}return r;}
function pB7(i,v,r7){
var r='',st='<div style="font-size:11px;margin-top:4px;',sb=st+'color:#888">',sg=st+'color:#52c41a">',sr=st+'color:#f5222d;font-weight:600">',sy=st+'color:#d48806;font-weight:600">',sw=st+'color:#d48806">';
switch(i){
case 0:r+=sg+'CC 就绪：'+(v>>7&1?'新数据':'无')+'</div>'+st+'color:#888">芯片故障：'+(v>>5&1?'错误':'正常')+'</div>'+st+'color:#888">UV: '+(v>>3&1?'触发':'正常')+'</div>'+st+'color:#888">OV: '+(v>>2&1?'触发':'正常')+'</div>'+st+'color:#888">SCD: '+(v>>1&1?'触发':'正常')+'</div>'+st+'color:#888">OCD: '+(v&1?'触发':'正常')+'</div>';break;
case 1:r+=st+'color:#888">均衡状态:</div>';for(var j=0;j<5;j++)r+=sw+'Cell'+(j+1)+': '+(((v>>j)&1)?'均衡中':'关闭')+'</div>';break;
case 2:r+=st+'color:#888">负载检测：'+(v>>7&1?'有':'无')+'</div>'+sb+'ADC: '+(v>>4&1?'使能':'禁用')+'</div>';break;
case 3:r+=sg+'放电 MOS: '+((v>>1)&1?'开启':'关闭')+'</div>'+sg+'充电 MOS: '+(v&1?'开启':'关闭')+'</div>';break;
case 4:var rs=(v>>7)&1,sd=['70us','100us','200us','400us'],sc=v&7,scdL=[22,33,44,56,67,78,89,100],scdH=[44,67,89,111,133,155,178,200],scdMv=rs?scdH[sc]:scdL[sc];r+=sg+'量程：'+(rs?'高 (x2)':'低')+'</div>'+sb+'短路延时：'+sd[(v>>3)&3]+'</div>'+sb+'短路阈值：'+scdMv*200+' mA ('+scdMv+' mV)</div>';break;
case 5:var p1=r7[4]||0,rs2=(p1>>7)&1,ocd=v&0x0F,ocdL=[8,11,14,17,19,22,25,28,31,33,36,39,42,44,47,50],ocdH=[17,22,28,33,39,44,50,56,61,67,72,78,83,89,94,100],ocdMv=rs2?ocdH[ocd]:ocdL[ocd];r+=sb+'过流延时：~'+(8<<((v>>4)&7))+'ms</div>'+sb+'过流阈值：'+ocdMv*200+' mA ('+ocdMv+' mV)</div>';break;
case 6:r+=sb+'欠压延时：'+[1,4,8,16][(v>>6)&3]+'s</div>'+sb+'过压延时：'+[1,2,4,8][(v>>4)&3]+'s</div>';break;
case 7:var g7=r7[10]||0,o7=r7[11]||0,gUv=365+g7,oS=o7>127?o7-256:o7,ovAdc=(v<<4)|0x2008,ovMv=(ovAdc*gUv)/1000+oS;r+=sr+'OV 阈值：'+ovMv.toFixed(1)+' mV (ADC:0x'+ovAdc.toString(16).toUpperCase()+')</div>';break;
case 8:var g8=r7[10]||0,o8=r7[11]||0,gU8=365+g8,oS8=o8>127?o8-256:o8,uvAdc=(v<<4)|0x1000,uvMv=(uvAdc*gU8)/1000+oS8;r+=sy+'UV 阈值：'+uvMv.toFixed(1)+' mV (ADC:0x'+uvAdc.toString(16).toUpperCase()+')</div>';break;
case 9:var cc=v&0x3F;r+=sb+'CC 配置：0x'+cc.toString(16).toUpperCase()+' '+(cc===0x19?'正确':'异常')+'</div>';break;
case 10:var g=v+365;r+=sb+'增益：'+g+' ('+(g/1000).toFixed(3)+' mV/LSB)</div>';break;
case 11:r+=sb+'偏移：'+v+' mV</div>';break;
}return r;}
function fmtUp(s){var m=Math.floor(s/60),h=Math.floor(m/60),d=Math.floor(h/24);if(d>0)return d+L.jsDays+(h%24)+L.jsHours;if(h>0)return h+L.jsHours+(m%60)+L.jsMinutes;if(m>0)return m+L.jsMinutes+(s%60)+L.jsSeconds;return s+L.jsSeconds;}
// === 配置：时间窗口 ===
var cw=[],wc=0;
var weekdayNames=['周日','周一','周二','周三','周四','周五','周六'];
var days=[{v:1},{v:2},{v:4},{v:8},{v:16},{v:32},{v:64}];
document.addEventListener('DOMContentLoaded',function(){initW(window.IW||[])});
function initW(w){cw=w;wc=cw.length;renW();updB()}
function addW(){if(cw.length>=5)return;cw.push({id:wc++,day_mask:0,start_hour:8,end_hour:20});renW();updB()}
function rmW(i){cw.splice(i,1);renW();updB()}
function recW(i){var m=0;$('wc').querySelectorAll('input[data-wi="'+i+'"]').forEach(function(c){if(c.checked)m|=+c.getAttribute('data-db')});cw[i].day_mask=m;renW()}
function renW(){
var c=$('wc');c.innerHTML='';
if(!cw.length){c.innerHTML='<p style="color:#bbb;text-align:center;padding:14px">'+L.jsNoWindows+'</p>';return}
cw.forEach(function(w,i){
var m='',selS='',selE='';
for(var h=0;h<24;h++)selS+='<option value="'+h+'"'+(h===w.start_hour?' selected':'')+'>'+(h<10?'0':'')+h+':00</option>';
for(var h=0;h<24;h++)selE+='<option value="'+h+'"'+(h===w.end_hour?' selected':'')+'>'+(h<10?'0':'')+h+':00</option>';
days.forEach(function(d,di){var ck=(w.day_mask&d.v)?'checked':'';m+='<label style="display:inline-block;margin:3px"><input type="checkbox" data-wi="'+i+'" data-db="'+d.v+'" '+ck+' onchange="recW('+i+')"> '+weekdayNames[di]+'</label>';});
c.innerHTML+='<div style="border:1px solid #e8e8e8;border-radius:6px;padding:10px;margin-bottom:10px;background:#fafafa"><div style="font-weight:600;margin-bottom:6px;color:#333;font-size:12px">'+L.jsWindow+(i+1)+'</div><div style="margin-bottom:6px"><strong style="color:#888;font-size:11px">'+L.jsWeekday+'</strong><br>'+m+'</div><div style="display:flex;align-items:center;gap:10px;margin-bottom:6px"><div style="font-size:12px"><strong style="color:#888">'+L.jsStart+'</strong><select onchange="cw['+i+'].start_hour=parseInt(this.value)">'+selS+'</select></div><div style="font-size:12px"><strong style="color:#888">'+L.jsEnd+'</strong><select onchange="cw['+i+'].end_hour=parseInt(this.value)">'+selE+'</select></div><button type="button" class="btn btn-r" style="padding:4px 10px;font-size:11px" onclick="rmW('+i+')">'+L.jsDelete+'</button></div><div style="font-size:10px;color:#bbb">Mask: 0x'+w.day_mask.toString(16).toUpperCase().padStart(2,'0')+'</div></div>';
});
}
function updB(){var b=$('ab');if(cw.length>=5){b.disabled=true;b.textContent=L.jsFull;b.style.opacity='.5'}else{b.disabled=false;b.textContent=L.jsAddWindow+' ('+cw.length+'/5)';b.style.opacity='1'}}
function toggleIP(){$('sIP').style.display=$('ipMode').value==='static'?'block':'none'}
// === 配置：保存 ===
function gVal(id){return $(id).value}
function gChk(id){return $(id).checked}

function isWifiCfgChg(){return gVal('ws')!==iwCfg.ssid||gVal('wp')!==iwCfg.pass||$('ipMode').value!==iwCfg.ipMode||gVal('sip')!==iwCfg.staticIp||gVal('sgw')!==iwCfg.staticGateway||gVal('ssn')!==iwCfg.staticSubnet||gVal('sdns')!==iwCfg.staticDns}

function save(){
var d={
system:{wifi_ssid:gVal('ws'),wifi_pass:gVal('wp'),use_static_ip:$('ipMode').value==='static',static_ip:gVal('sip'),static_gateway:gVal('sgw'),static_subnet:gVal('ssn'),static_dns:gVal('sdns'),ntp_server:gVal('ntp'),buzzer_enabled:gChk('be'),volume_level:+gVal('vl'),led_brightness:+gVal('lb'),hid_enabled:gChk('hid_en'),hid_report_mode:+gVal('hid_mode'),mqtt_enabled:gChk('mqtt_en'),mqtt_broker:gVal('mqtt_brk'),mqtt_port:+gVal('mqtt_port'),mqtt_username:gVal('mqtt_usr'),mqtt_password:gVal('mqtt_pwd'),xiaomi_sensor_enabled:gChk('xiaomi_en')},
bms:{chemistry:gVal('bchem'),cell_count:+gVal('bc'),nominal_capacity_mAh:+gVal('bn'),cell_ov_threshold:+gVal('bo'),cell_uv_threshold:+gVal('bu'),cell_ov_recover:+gVal('bor'),cell_uv_recover:+gVal('bur'),max_charge_current:+gVal('bmc'),max_discharge_current:+gVal('bmd'),short_circuit_threshold:+gVal('bsc'),temp_overheat_threshold:+gVal('both'),balancing_enabled:gChk('bbe'),balancing_voltage_diff:+gVal('bbd')},
power:{max_charge_current:+gVal('pmc'),charge_voltage_limit:+gVal('pcv'),charge_soc_start:+gVal('pcs'),charge_soc_stop:+gVal('pcp'),max_discharge_current:+gVal('pmd'),discharge_soc_stop:+gVal('pds'),enable_hybrid_boost:gChk('phe'),vsys_min_mV:+gVal('pvsm'),over_current_threshold:+gVal('poc'),over_temp_threshold:+gVal('pot'),charge_temp_high_limit:+gVal('pth'),charge_temp_low_limit:+gVal('ptl'),charging_windows:cw,charging_window_count:cw.length}};

if(isWifiCfgChg()){alert(L.jsSaveNetChange);fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).catch(function(){});setTimeout(function(){location.href='/'},2e3)}else{fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(function(r){return r.json()}).then(function(r){if(r.success){var msg=r.message||L.jsSaveSuccess;if(r.restart_required){alert(msg+' '+L.jsRestarting);setTimeout(function(){location.href='/'},3e3)}else{alert(msg)}}else{alert(L.jsSaveFailed+(r.message||''))}}).catch(function(){alert(L.jsNetworkErr)})}
}
// === OTA: 固件上传 ===
var sel=null,area=$('upArea');
area.addEventListener('dragover',function(e){e.preventDefault();this.classList.add('drag')});
area.addEventListener('dragleave',function(e){e.preventDefault();this.classList.remove('drag')});
area.addEventListener('drop',function(e){e.preventDefault();this.classList.remove('drag');if(e.dataTransfer.files.length)handle(e.dataTransfer.files[0])});
function selFile(inp){if(inp.files.length)handle(inp.files[0])}
function handle(f){if(!f.name.endsWith('.bin')){showOtaMsg(L.jsSelectBin,'er');return}sel=f;$('fName').textContent=f.name+' ('+(f.size/1024).toFixed(1)+' KB)';$('upBtn').disabled=false;showOtaMsg(L.jsReady,'nf')}
function showOtaMsg(m,t){var s=$('stMsg');s.textContent=m;s.className='oms '+t}
function upload(){
if(!sel)return;var fd=new FormData();fd.append('firmware',sel,sel.name);var xhr=new XMLHttpRequest();
xhr.open('POST','/firmware',true);
xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('prgF').style.width=p+'%';$('prgF').textContent=p+'%'}};
xhr.onloadstart=function(){$('prgC').style.display='block';$('upBtn').disabled=true;$('upBtn').textContent=L.jsUpgrading;showOtaMsg(L.jsUploading,'nf')};
xhr.onload=function(){if(xhr.status===200){showOtaMsg(L.jsUpgradeSuccess,'ok');setTimeout(function(){location.href='/'},3e3)}else{showOtaMsg('HTTP '+xhr.status,'er');$('upBtn').disabled=false;$('upBtn').textContent=L.jsRetry}};
xhr.onerror=function(){showOtaMsg(L.jsNetworkError,'er');$('upBtn').disabled=false;$('upBtn').textContent=L.jsRetry};
xhr.send(fd);}

// === 配置向导 (手机适配) ===
var wzCurStep=0,wzTotalSteps=5;
var wzData={wifi:{ssid:'',pass:''},network:{mode:'dhcp',ip:'',gateway:'',subnet:'',dns:'',ntp:'ntp.aliyun.com'},battery:{chemistry:'ncm',cells:3,capacity:2000,chargeCur:2000,dischargeCur:12000,ovThresh:4210,uvThresh:3000,balancing:true},hardware:{buzzer:true,volume:70,brightness:80,hid:true,hidMode:0}};

function wzShowStep(n){
wzCurStep=n;
document.querySelectorAll('.wz-step').forEach(function(s,i){s.classList.remove('active','completed');if(i<n)s.classList.add('completed');if(i===n)s.classList.add('active')});
document.querySelectorAll('.wz-card').forEach(function(c,i){c.style.display=i===n?'block':'none'});
$('wz-prev').disabled=n===0;
var btn=$('wz-next');if(n===wzTotalSteps-1){btn.textContent=L.wzSaveRestart;btn.className='wz-btn wz-btn-save'}else{btn.textContent=L.wzNext;btn.className='wz-btn wz-btn-next'}
}
function wzPrev(){if(wzCurStep>0)wzShowStep(wzCurStep-1);}
function wzNext(){if(wzCurStep<wzTotalSteps-1){if(!wzValidateStep(wzCurStep))return;wzSaveStep(wzCurStep);wzShowStep(wzCurStep+1)}else{wzSaveAll()}}
function wzSaveStep(n){if(n===1)wzToggleIP();}
function wzGetVal(id){return $(id).value}
function wzValidateStep(n){
if(n===0){var ss=$('wz-wifi-ssid').value.trim();if(!ss){alert(L.wzWifiSsidPh);return false}wzData.wifi={ssid:ss,pass:wzGetVal('wz-wifi-pass')};
var au=$('wz-auth-user').value.trim(),ap=$('wz-auth-pass').value,ap2=$('wz-auth-pass2').value;
if(!au){alert(L.jsAuthUserRequired);return false}
if(ap.length<8){alert(L.jsAuthPassLen);return false}
if(ap!==ap2){alert(L.jsAuthPassMismatch);return false}
wzData.auth={username:au,password:ap}}
if(n===1){wzData.network.mode=wzGetVal('wz-ip-mode');wzData.network.ntp=wzGetVal('wz-ntp-server');if(wzData.network.mode==='static'){var ip=wzGetVal('wz-static-ip-addr');if(!ip){alert(L.jsEnterStaticIp);return false}wzData.network={ip:ip,gateway:wzGetVal('wz-static-gateway'),subnet:wzGetVal('wz-static-subnet'),dns:wzGetVal('wz-static-dns'),ntp:wzData.network.ntp}}}
if(n===2){var cap=+$('wz-capacity').value;if(!cap||cap<100){alert(L.jsEnterCapacity);return false}var ct=wzGetVal('wz-chemistry')||'ncm',cm=CHEM[ct];var ov=+$('wz-ov-threshold').value||cm.recOv,uv=+$('wz-uv-threshold').value||cm.recUv;if(ov<cm.ovMin||ov>cm.ovMax){alert(L.jsOvRange);return false}if(uv<cm.uvMin||uv>cm.uvMax){alert(L.jsUvRange);return false}wzData.battery={chemistry:ct,cells:+$('wz-cell-count').value,capacity:cap,chargeCur:+$('wz-charge-current').value||1000,dischargeCur:+$('wz-discharge-current').value||2000,ovThresh:ov,uvThresh:uv,balancing:$('wz-balancing').checked}}
if(n===3){wzData.hardware={buzzer:$('wz-buzzer').checked,volume:+$('wz-volume').value,brightness:+$('wz-brightness').value,hid:$('wz-hid').checked,hidMode:+$('wz-hid-mode').value}}
return true;
}
function wzToggleIP(){var d=$('wz-static-ip');d.style.display=$('wz-ip-mode').value==='static'?'block':'none'}
function wzSaveAll(){
var doc={
auth:wzData.auth||{},
system:{wifi_ssid:wzData.wifi.ssid,wifi_pass:wzData.wifi.pass,use_static_ip:wzData.network.mode==='static',static_ip:wzData.network.ip||'',static_gateway:wzData.network.gateway||'',static_subnet:wzData.network.subnet||'',static_dns:wzData.network.dns||'',ntp_server:wzData.network.ntp,buzzer_enabled:wzData.hardware.buzzer,volume_level:wzData.hardware.volume,led_brightness:wzData.hardware.brightness,hid_enabled:wzData.hardware.hid,hid_report_mode:wzData.hardware.hidMode},
bms:{chemistry:wzData.battery.chemistry||'ncm',cell_count:wzData.battery.cells,nominal_capacity_mAh:wzData.battery.capacity,max_charge_current:wzData.battery.chargeCur,max_discharge_current:wzData.battery.dischargeCur,cell_ov_threshold:wzData.battery.ovThresh,cell_uv_threshold:wzData.battery.uvThresh,balancing_enabled:wzData.battery.balancing},
power:{max_charge_current:wzData.battery.chargeCur,max_discharge_current:wzData.battery.dischargeCur}};
var r=new XMLHttpRequest();r.open('POST','/save',true);r.setRequestHeader('Content-Type','application/json');r.timeout=15000;
r.onload=function(){try{var j=JSON.parse(r.responseText);if(j.success){$('wz-nav').style.display='none';startRebootCountdown()}else{alert(L.jsSaveFailed+(j.message||''))}}catch(e){startRebootCountdown()}};
r.onerror=r.ontimeout=function(){startRebootCountdown()};
r.send(JSON.stringify(doc));
}
function startRebootCountdown(){var c=5,el=$('wz-reboot-count');var t=setInterval(function(){c--;el.textContent=c;if(c<=0){clearInterval(t);el.textContent=L.jsRebooting;setTimeout(function(){location.href='/'},3e3)}},1e3)}

// === 退出登录 ===
function logout(){
if(!confirm(L.jsConfirmLogout))return;
fetch('/api/auth/logout',{method:'POST'}).then(function(){location.href='/'}).catch(function(){location.href='/'})
}

// === 修改访问账户 ===
function changeAuth(){
var u=$('auth_user').value.trim(),p=$('auth_pass').value,p2=$('auth_pass2').value,st=$('authStatus');
st.style.color='#f5222d';
if(!u){st.textContent=L.jsAuthUserRequired;return}
if(p.length<8){st.textContent=L.jsAuthPassLen;return}
if(p!==p2){st.textContent=L.jsAuthPassMismatch;return}
st.style.color='#1677ff';st.textContent=L.jsProcessing;
fetch('/api/auth/change',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})})
.then(function(r){return r.json()})
.then(function(r){if(r.success){st.style.color='#52c41a';st.textContent=L.jsAuthChanged;setTimeout(function(){location.reload()},2e3)}else{st.style.color='#f5222d';st.textContent=r.message||L.jsSaveFailed}})
.catch(function(){st.style.color='#f5222d';st.textContent=L.jsNetworkErr})
}

// === 清除提示 ===
function clearTips(){var x=new XMLHttpRequest();x.open('POST','/api/clear-tips',true);x.setRequestHeader('Content-Type','application/json');x.timeout=3e3;x.send('')}

// === 运输模式 ===
function enterShipMode(){if(!confirm(L.jsConfirmShip))return;var x=new XMLHttpRequest();x.open('POST','/bms/shipmode',true);x.setRequestHeader('Content-Type','application/json');x.timeout=5e3;x.onload=function(){if(x.status===200){try{var j=JSON.parse(x.responseText);if(j.success){alert(L.jsShipSuccess)}else{alert(L.jsResetFailed+(j.message||''))}}catch(e){alert(L.jsShipSuccess)}}else{alert('HTTP '+x.status)}};x.onerror=function(){alert(L.jsNetworkError)};x.ontimeout=function(){alert(L.jsNetworkError)};x.send('')}

// === 电池数据重置 ===
function resetBatteryData(){if(!confirm(L.jsConfirmReset))return;$('resetBmsStatus').style.color='#1677ff';$('resetBmsStatus').textContent=L.jsProcessing;var x=new XMLHttpRequest();x.open('POST','/bms/reset-data',true);x.setRequestHeader('Content-Type','application/json');x.timeout=5e3;x.onload=function(){if(x.status===200){try{var j=JSON.parse(x.responseText);if(j.success){$('resetBmsStatus').style.color='#52c41a';$('resetBmsStatus').textContent=L.jsResetSuccess}else{$('resetBmsStatus').style.color='#f5222d';$('resetBmsStatus').textContent=L.jsResetFailed+(j.message||'')}}catch(e){$('resetBmsStatus').style.color='#52c41a';$('resetBmsStatus').textContent=L.jsResetSuccess}}else{$('resetBmsStatus').style.color='#f5222d';$('resetBmsStatus').textContent='HTTP '+x.status}};x.onerror=function(){$('resetBmsStatus').style.color='#f5222d';$('resetBmsStatus').textContent=L.jsNetworkError};x.ontimeout=function(){$('resetBmsStatus').style.color='#f5222d';$('resetBmsStatus').textContent=L.jsNetworkError};x.send('')}

// === 重启设备 ===
function restartDevice(){
if(curPowerMode!==0){alert(L.jsNoAcRestart);return}
if(!confirm(L.jsConfirmRestart))return;
var x=new XMLHttpRequest();x.open('POST','/api/restart',true);x.setRequestHeader('Content-Type','application/json');x.timeout=5e3;
x.onload=function(){if(x.status===200){try{var j=JSON.parse(x.responseText);if(j.success){alert(L.jsRestarting)}else{alert(L.jsResetFailed+(j.message||''))}}catch(e){alert(L.jsRestarting)}}else if(x.status===403){alert(L.jsNoAcRestart)}else{alert('HTTP '+x.status)}};
x.onerror=function(){alert(L.jsNetworkError)};
x.ontimeout=function(){alert(L.jsNetworkError)};
x.send('')}

// === ADC 校准系数 ===
var calDescs=['输入电流检测','放电电流检测','系统功率监测','输入电压','电池电压','主板温度 (NTC)','环境温度 (NTC)'];
var calPins=[{name:'BQ24780S_IADP',pin:1},{name:'BQ24780S_IDCHG',pin:2},{name:'BQ24780S_PMON',pin:9},{name:'INPUT_VOLTAGE',pin:4},{name:'BATTERY_VOLTAGE',pin:5},{name:'BOARD_TEMP',pin:7},{name:'ENVIRONMENT_TEMP',pin:8}];
var calData=null;
function loadCalibration(){$('calStatus').textContent='';fetch('/api/calibration').then(function(r){return r.json()}).then(function(d){if(d.success){calData=d.calibration;renderCalibration()}}).catch(function(){$('calStatus').textContent='加载失败'})}
function renderCalibration(){var c=$('calContainer');c.innerHTML='';calPins.forEach(function(p,i){var v=calData[i]||100;c.innerHTML+='<div style="border:1px solid #e8e8e8;border-radius:6px;padding:12px"><div style="font-weight:600;font-size:13px;margin-bottom:4px">'+calDescs[i]+'</div><div style="font-size:11px;color:#888;margin-bottom:8px">'+p.name+' (GPIO'+p.pin+')</div><div style="display:flex;align-items:center;gap:8px"><input type="range" min="50" max="255" value="'+v+'" id="cal_'+i+'" style="flex:1" oninput="document.getElementById(\'calv_'+i+'\').textContent=(this.value/100).toFixed(2)+\'x\'"><span id="calv_'+i+'" style="min-width:50px;font-weight:600;color:#1677ff">'+(v/100).toFixed(2)+'x</span></div></div>'})}
function saveCalibration(){var vals=[];for(var i=0;i<calPins.length;i++){var v=+$('cal_'+i).value;if(v<50||v>255){alert(L.jsCalRange);return}vals.push(v)}fetch('/api/calibration',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({calibration:vals})}).then(function(r){return r.json()}).then(function(d){if(d.success){$('calStatus').style.color='#52c41a';$('calStatus').textContent=L.jsCalSaved}else{$('calStatus').style.color='#f5222d';$('calStatus').textContent=L.jsCalFailed+(d.message||'')}}).catch(function(){$('calStatus').textContent=L.jsNetworkError})}

if(window.CONFIG_MODE===1){document.addEventListener('DOMContentLoaded',function(){var m=document.querySelector('.main-wrap'),sd=document.querySelector('.side'),ct=document.querySelector('.ct'),w=$('wz-page'),f=document.querySelector('.foot'),t=document.querySelector('.topbar-info');if(sd)sd.style.display='none';if(ct)ct.style.display='none';if(m){m.style.display='block';m.style.overflow='auto'}if(w)w.style.display='block';if(f)f.style.display='none';if(t)t.style.display='none'})}

// === 原始采样数据文件 ===
function loadRawFiles(){
$('rawFileList').innerHTML='<span style="color:#1677ff">'+L.jsLoading+'</span>';
fetch('/api/raw-files').then(function(r){return r.json()}).then(function(d){
if(!d.success||!d.files||d.files.length===0){$('rawFileList').innerHTML='<span style="color:#bbb">'+L.jsNoData+'</span>';return}
var h='<div style="max-height:250px;overflow-y:auto">';
for(var i=0;i<d.files.length;i++){
var f=d.files[i];
var sz=f.size>=1024?(f.size/1024).toFixed(1)+' KB':f.size+' B';
var dlUrl='/api/raw-file?name='+encodeURIComponent(f.name);
var baseName=f.name.split('/').pop();
h+='<div style="display:flex;align-items:center;justify-content:space-between;padding:6px 8px;border-bottom:1px solid #f0f0f0">';
h+='<a href="'+dlUrl+'" download="'+baseName+'" style="color:#1677ff;text-decoration:none">'+f.name+'</a>';
h+='<span style="color:#999;font-size:12px">'+sz+'</span>';
h+='</div>';
}
h+='</div>';
$('rawFileList').innerHTML=h;
}).catch(function(){$('rawFileList').innerHTML='<span style="color:#f5222d">'+L.jsLoadFailed+'</span>'});
}

// === 日志文件 ===
function loadLogFiles(){
$('logFileList').innerHTML='<span style="color:#1677ff">'+L.jsLoading+'</span>';
fetch('/api/log-files').then(function(r){return r.json()}).then(function(d){
if(!d.success||!d.files||d.files.length===0){$('logFileList').innerHTML='<span style="color:#bbb">'+L.jsNoData+'</span>';return}
var h='<div style="max-height:250px;overflow-y:auto">';
for(var i=0;i<d.files.length;i++){
var f=d.files[i];
var sz=f.size>=1024?(f.size/1024).toFixed(1)+' KB':f.size+' B';
var dlUrl='/api/log-file?name='+encodeURIComponent(f.name);
var baseName=f.name.split('/').pop();
h+='<div style="display:flex;align-items:center;justify-content:space-between;padding:6px 8px;border-bottom:1px solid #f0f0f0">';
h+='<a href="'+dlUrl+'" download="'+baseName+'" style="color:#1677ff;text-decoration:none">'+f.name+'</a>';
h+='<span style="color:#999;font-size:12px">'+sz+'</span>';
h+='</div>';
}
h+='</div>';
$('logFileList').innerHTML=h;
}).catch(function(){$('logFileList').innerHTML='<span style="color:#f5222d">'+L.jsLoadFailed+'</span>'});
}

conn();
)rawliteral";

#endif // JS_TEMPLATES_H