#ifndef PAGE_TEMPLATES_H
#define PAGE_TEMPLATES_H

#include <Arduino.h>
#include "js_templates.h"

// =============================================================================
// SPA 单页模板 - HTML
// =============================================================================
const char SPA_PAGE_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS 控制中心</title>
<style id="dynamic-css"></style>
</head>
<body>
<div class="toast-container" id="toastBox"></div>
<div class="topbar">
<h1 data-i18n="pageTitle">UPS 控制中心</h1>
<div class="topbar-info">
<span class="ws-st" id="wsSt">连接中</span>
<span id="uptime">--</span>
<span id="wifi">--</span>
<span id="rssi">-- dBm</span>
</div>
</div>
<div class="tip-bar" id="tipBar"></div>
<div class="main-wrap">
<div class="side">
<div class="side-nav">
<div class="si active" data-i18n="navStatus" onclick="show('status',this)">📊 状态概览</div>
<div class="si" data-i18n="navBms" onclick="show('bms',this)">🔋 BMS 状态</div>
<div class="si" data-i18n="navPower" onclick="show('power',this)">⚡ 电源状态</div>
<div class="si" data-i18n="navConfig" onclick="show('config',this)">⚙️ 系统配置</div>
<div class="si" data-i18n="navOta" onclick="show('ota',this)">📦 固件升级</div>
</div>
<div class="side-restart" data-i18n="btnRestart" onclick="restartDevice()">重启设备</div>
<div class="side-restart" data-i18n="btnLogout" onclick="logout()" style="margin-top:6px">退出登录</div>
</div>
<div class="ct">
<!-- ===== 面板：状态概览 ===== -->
<div class="pnl active" id="p-status">
<div class="grid">
<div class="card"><div class="card-t" data-i18n="cardBattery">电池状态</div>
<div class="bigv" id="soc">--<span class="bigu">%</span></div>
<div class="pb"><div class="pf" id="socBar">0%</div></div>
<div class="row"><span class="lb" data-i18n="lblVoltage">电压</span><span class="vl" id="battV">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblCurrent">电流</span><span class="vl" id="battI">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblTemp">温度</span><span class="vl" id="battT">-- °C</span></div>
<div class="row"><span class="lb" data-i18n="lblHealth">健康度</span><span class="vl" id="soh">-- %</span></div>
<div class="row"><span class="lb" data-i18n="lblCycles">循环次数</span><span class="vl" id="cycles">--</span></div>
<div class="row"><span class="lb" data-i18n="lblCapacity">剩余容量</span><span class="vl" id="capR">-- mAh</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardCells">单体电压</div>
<div class="cells" id="cells"></div>
<div class="row" style="margin-top:8px"><span class="lb" data-i18n="lblMaxMin">最高 / 最低</span><span class="vl" id="cellMM">--/-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblDiff">压差</span><span class="vl" id="cellD">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblBalance">均衡状态</span><span class="vl" id="balSt">--</span></div>
<div class="row"><span class="lb" data-i18n="lblBalTotal">总均衡次数</span><span class="vl" id="balTotal">--</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardPower">电源状态</div>
<div class="row"><span class="lb" data-i18n="lblAc">AC 电源</span><span class="vl" id="acSt">--</span></div>
<div class="row"><span class="lb" data-i18n="lblInVoltage">输入电压</span><span class="vl" id="inV">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblInCurrent">输入电流</span><span class="vl" id="inI">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblOutPower">输出功率</span><span class="vl" id="outP">-- W</span></div>
<div class="row"><span class="lb" data-i18n="lblPmVoltage">电池电压 (PM)</span><span class="vl" id="pmBattV">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblPmCurrent">放电电流 (PM)</span><span class="vl" id="pmBattI">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblChargeStatus">充电状态</span><span class="vl" id="chgSt">--</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardSystem">系统状态</div>
<div class="row"><span class="lb" data-i18n="lblPowerMode">运行模式</span><span class="vl" id="pwrMd">--</span></div>
<div class="row"><span class="lb" data-i18n="lblBoardTemp">板温</span><span class="vl" id="brdT">-- °C</span></div>
<div class="row"><span class="lb" data-i18n="lblEnvTemp">环境温度</span><span class="vl" id="envT">-- °C</span></div>
<div class="row"><span class="lb" data-i18n="lblShtTemp">SHTC3温度</span><span class="vl" id="shtT">-- °C</span></div>
<div class="row"><span class="lb" data-i18n="lblShtHumid">SHTC3湿度</span><span class="vl" id="shtH">-- %</span></div>
</div>
</div>
<div class="card" style="margin-top:16px">
<div class="card-t" data-i18n="cardLogFiles">日志文件</div>
<p style="color:#888;font-size:12px;margin:4px 0 10px" data-i18n="logFilesDesc">系统日志文件，保存在 SPIFFS 分区 /log/ 目录下，保留 7 天</p>
<button type="button" class="btn" style="margin-bottom:10px;font-size:12px;padding:4px 12px" onclick="loadLogFiles()" data-i18n="btnRefreshLog">刷新日志文件列表</button>
<div id="logFileList" style="font-size:13px;color:#666" data-i18n="btnClickLoad">点击上方按钮加载</div>
</div>
</div>

<!-- ===== 面板：BMS 状态 (含 BQ76920 寄存器) ===== -->
<div class="pnl" id="p-bms">
<div class="grid">
<div class="card"><div class="card-t" data-i18n="cardBattery">电池状态</div>
<div class="bigv" id="b_soc">--<span class="bigu">%</span></div>
<div class="pb"><div class="pf" id="b_soc_bar">0%</div></div>
<div class="row"><span class="lb" data-i18n="lblHealth">健康度</span><span class="vl" id="b_soh">-- %</span></div>
<div class="row"><span class="lb" data-i18n="lblTemp">温度</span><span class="vl" id="b_t">-- °C</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardVoltageCurrent">电压 & 电流</div>
<div class="row"><span class="lb" data-i18n="lblTotalVoltage">总电压</span><span class="vl" id="b_v">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblCurrent">电流</span><span class="vl g" id="b_i">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblCycles">循环次数</span><span class="vl" id="b_cyc">--</span></div>
<div class="row"><span class="lb" data-i18n="lblCapacity">剩余容量</span><span class="vl" id="b_cap">-- mAh</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardProtection">保护 & 均衡</div>
<div class="row"><span class="lb" data-i18n="lblBalance">均衡状态</span><span class="vl" id="b_bal">--</span></div>
<div class="row"><span class="lb" data-i18n="lblBalTotal">总均衡次数</span><span class="vl" id="b_bal_total">--</span></div>
<div class="row"><span class="lb" data-i18n="lblFaultType">故障类型</span><span class="vl r" id="b_fault">--</span></div>
<div class="row"><span class="lb" data-i18n="lblBmsMode">BMS 模式</span><span class="vl" id="b_mode">--</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardSelfConsumption">系统自消耗（实验性功能）</div>
<div class="row"><span class="lb" data-i18n="lblScCurrent">消耗电流</span><span class="vl" id="sc_mA">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblLastUpdate">最后更新</span><span class="vl" id="sc_time">--</span></div>
</div>
</div>
<div class="card" style="margin-top:16px"><div class="card-t" data-i18n="cardCellDetail">单体电压详情</div>
<div class="cells" id="b_cells"></div>
<div class="grid" style="grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;margin-top:12px">
<div class="stat-box"><div class="stat-label" data-i18n="lblMax">最高</div><div class="stat-value" id="b_max">-- mV</div></div>
<div class="stat-box"><div class="stat-label" data-i18n="lblMin">最低</div><div class="stat-value" id="b_min">-- mV</div></div>
<div class="stat-box"><div class="stat-label" data-i18n="lblAvg">平均</div><div class="stat-value" id="b_avg">-- mV</div></div>
<div class="stat-box"><div class="stat-label" data-i18n="lblDiff">压差</div><div class="stat-value" id="b_dlt">-- mV</div></div>
<div class="stat-box"><div class="stat-label" data-i18n="lblIrSample">内阻采样（实验性功能）</div><div class="stat-value" id="b_ir_cnt">--</div></div>
</div>
</div>
<div class="card-t" style="margin:20px 0 12px;font-size:14px;font-weight:600" data-i18n="cardBq76920Regs">BQ76920 寄存器状态</div>
<div id="r76" class="grid"></div>

<div class="card" style="margin-top:16px">
<div class="card-t" data-i18n="cardRawFiles">原始采样数据文件</div>
<p style="color:#888;font-size:12px;margin:4px 0 10px" data-i18n="rawFilesDesc">每分钟采集一次，保存在 SPIFFS 分区 /raw/ 目录下，保留 30 天</p>
<button type="button" class="btn" style="margin-bottom:10px;font-size:12px;padding:4px 12px" onclick="loadRawFiles()" data-i18n="btnRefreshRaw">刷新采样文件列表</button>
<div id="rawFileList" style="font-size:13px;color:#666" data-i18n="btnClickLoad">点击上方按钮加载</div>
</div>


</div>

<!-- ===== 面板：电源状态 (含 BQ24780S 寄存器) ===== -->
<div class="pnl" id="p-power">
<div class="grid">
<div class="card"><div class="card-t" data-i18n="cardInputPower">输入电源</div>
<div class="row"><span class="lb" data-i18n="lblAcStatus">AC 状态</span><span class="vl" id="p_ac">--</span></div>
<div class="row"><span class="lb" data-i18n="lblInVoltage">输入电压</span><span class="vl" id="p_iv">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblInCurrent">输入电流</span><span class="vl" id="p_ii">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblInputPower">输入功率</span><span class="vl" id="p_ip">-- W</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardBatteryMon">电池监测</div>
<div class="row"><span class="lb" data-i18n="lblPmVoltage">电池电压</span><span class="vl" id="p_bv">-- mV</span></div>
<div class="row"><span class="lb" data-i18n="lblPmCurrent">放电电流</span><span class="vl g" id="p_bi">-- mA</span></div>
<div class="row"><span class="lb" data-i18n="lblOutPower">输出功率</span><span class="vl" id="p_op">-- W</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardChargeCtrl">充电控制</div>
<div class="row"><span class="lb" data-i18n="lblChargeEnable">充电使能</span><span class="vl" id="p_ce">--</span></div>
<div class="row"><span class="lb" data-i18n="lblHybrid">混合模式</span><span class="vl" id="p_hy">--</span></div>
<div class="row"><span class="lb" data-i18n="lblFaultType">故障类型</span><span class="vl r" id="p_ft">--</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="cardChipStatus">芯片状态</div>
<div class="row"><span class="lb">PROCHOT</span><span class="vl" id="p_ph">--</span></div>
<div class="row"><span class="lb">TBSTAT</span><span class="vl" id="p_tb">--</span></div>
</div>
</div>
<div class="card-t" id="regTitle" style="margin:20px 0 12px;font-size:14px;font-weight:600">BQ24780S <span data-i18n="wzRegTitle">寄存器状态</span></div>
<div id="r24" class="grid"></div>
</div>

<!-- ===== 面板：系统配置 ===== -->
<div class="pnl" id="p-config">
<div style="display:flex;gap:0;margin-bottom:14px;background:#fff;border-radius:8px;border:1px solid #e8e8e8;overflow:hidden">
<div class="si active" data-i18n="cfgSystem" data-tab="system" onclick="showCfg('system',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">⚙️ 系统设置</div>
<div class="si" data-i18n="cfgHardware" data-tab="hardware" onclick="showCfg('hardware',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">🔊 硬件控制</div>
<div class="si" data-i18n="cfgBms" data-tab="bms" onclick="showCfg('bms',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">🔋 BMS 配置</div>
<div class="si" data-i18n="cfgWindows" data-tab="windows" onclick="showCfg('windows',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">⏰ 充电窗口</div>
<div class="si" data-i18n="cfgPower" data-tab="power" onclick="showCfg('power',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">⚡ 电源管理</div>
<div class="si" data-i18n="cfgAuth" data-tab="auth" onclick="showCfg('auth',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">🔐 访问账户</div>
<div class="si" data-i18n="cfgCalibration" data-tab="calibration" onclick="showCfg('calibration',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">📐 校准系数</div>
<div class="si" data-i18n="cfgShipping" data-tab="shipping" onclick="showCfg('shipping',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">📦 运输模式</div>
</div>

<div class="pnl active" id="p-cfg-system">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgLang">🌐 语言 / Language</legend>
<div class="fg"><label data-i18n="cfgLangLabel">界面语言:</label><select id="langSel" onchange="switchLang(this.value)"><option value="zh">中文</option><option value="en">English</option></select></div>
</fieldset>
<fieldset class="fs">
<legend class="lg" data-i18n="cfgWifi">📶 WiFi 设置</legend>
<div class="fg"><label data-i18n="cfgWifiName">WiFi 名称:</label><input type="text" id="ws" value="%WIFI_SSID%" required></div>
<div class="fg"><label data-i18n="cfgWifiPass">WiFi 密码:</label><input type="password" id="wp" value="%WIFI_PASS%"></div>
<div class="fg"><label data-i18n="cfgIpMode">IP 获取方式:</label><select id="ipMode" onchange="toggleIP()"><option value="dhcp"%IP_MODE_DHCP% data-i18n="cfgDhcp">动态获取 (DHCP)</option><option value="static"%IP_MODE_STATIC% data-i18n="cfgStaticIp">固定 IP</option></select></div>
</fieldset>
 <fieldset class="fs" id="sIP" style="display:%STATIC_IP_DISPLAY%">
<legend class="lg" data-i18n="cfgStaticIpTitle">🌐 静态 IP 配置</legend>
<div class="fg"><label data-i18n="cfgIpAddress">IP 地址:</label><input type="text" id="sip" value="%STATIC_IP%" placeholder="192.168.1.100"></div>
<div class="fg"><label data-i18n="cfgGateway">网关:</label><input type="text" id="sgw" value="%STATIC_GATEWAY%" placeholder="192.168.1.1"></div>
<div class="fg"><label data-i18n="cfgSubnet">子网掩码:</label><input type="text" id="ssn" value="%STATIC_SUBNET%" placeholder="255.255.255.0"></div>
<div class="fg"><label data-i18n="cfgDns">DNS 服务器:</label><input type="text" id="sdns" value="%STATIC_DNS%" placeholder="8.8.8.8"></div>
</fieldset>
<fieldset class="fs">
<legend class="lg" data-i18n="cfgNtp">🕐 时间同步配置</legend>
<div class="fg"><label data-i18n="cfgNtpServer">NTP 服务器:</label><input type="text" id="ntp" value="%NTP_SERVER%" placeholder="ntp.aliyun.com"></div>
</fieldset>
<fieldset class="fs">
<legend class="lg" data-i18n="cfgHid">🎮 HID 配置</legend>
<div class="fg"><label data-i18n="cfgHidService">HID 服务:</label><label class="cl"><input type="checkbox" id="hid_en" %HID_CHECKED%><span style="margin-left:8px" data-i18n="cfgEnabled">启用</span></label></div>
<div class="fg"><label data-i18n="cfgHidMode">电量模式:</label><select id="hid_mode"><option value="0"%HID_MODE_MAH%>mAh</option><option value="1"%HID_MODE_MWH%>mWh</option><option value="2"%HID_MODE_PCT% data-i18n="cfgPct">百分比 (%)（linux 选这个）</option></select></div>
</fieldset>
<fieldset class="fs">
<legend class="lg" data-i18n="cfgMqtt">📡 MQTT 配置（需要重启后生效）</legend>
<div class="fg"><label data-i18n="cfgMqttService">MQTT 服务:</label><label class="cl"><input type="checkbox" id="mqtt_en" %MQTT_CHECKED%><span style="margin-left:8px" data-i18n="cfgEnabled">启用</span></label></div>
<div class="fg"><label data-i18n="cfgMqttBroker">Broker 地址:</label><input type="text" id="mqtt_brk" value="%MQTT_BROKER%" placeholder="192.168.1.100"></div>
<div class="fg"><label data-i18n="cfgMqttPort">端口:</label><input type="number" id="mqtt_port" value="%MQTT_PORT%" min="1" max="65535" placeholder="1883"></div>
<div class="fg"><label data-i18n="cfgMqttUser">用户名:</label><input type="text" id="mqtt_usr" value="%MQTT_USERNAME%" placeholder="Optional"></div>
<div class="fg"><label data-i18n="cfgMqttPass">密码:</label><input type="password" id="mqtt_pwd" value="%MQTT_PASSWORD%" placeholder="Optional" autocomplete="new-password"></div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-hardware">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgHardware">🔊 硬件控制</legend>
<div class="fg"><label data-i18n="cfgBuzzer">🔔 蜂鸣器:</label><label class="cl"><input type="checkbox" id="be" %BUZZER_CHECKED%><span style="margin-left:8px" data-i18n="cfgEnabled">启用</span></label></div>
<div class="fg"><label data-i18n="cfgVolume">🔉 音量:</label><input type="range" id="vl" min="0" max="100" value="%VOLUME_VALUE%" oninput="document.getElementById('vv').textContent=this.value+'%'"><span id="vv" style="color:#888;font-size:12px;min-width:36px">%VOLUME_LEVEL%</span></div>
<div class="fg"><label data-i18n="cfgLedBrightness">💡 LED 亮度:</label><input type="range" id="lb" min="0" max="100" value="%LIGHT_VALUE%" oninput="document.getElementById('lv').textContent=this.value+'%'"><span id="lv" style="color:#888;font-size:12px;min-width:36px">%LIGHT_BRIGHTNESS%</span></div>
</fieldset>
<fieldset class="fs" id="xiaomiSection" style="display:%XIAOMI_SECTION_DISPLAY%">
<legend class="lg" data-i18n="cfgXiaomi">📡 小米传感器桥接</legend>
<p style="color:#aaa;font-size:12px;margin-bottom:8px" data-i18n="cfgXiaomiDesc">通过I2C从机模拟SHTC3传感器，供米家温湿度计2读取UPS数据。仅新版PCB支持。电池温度→温度，SOC→湿度。(配置后需要重启生效)</p>
<div class="fg"><label data-i18n="cfgSensorBridge">传感器桥接:</label><label class="cl"><input type="checkbox" id="xiaomi_en" %XIAOMI_CHECKED%><span style="margin-left:8px" data-i18n="cfgEnabled">启用</span></label></div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-bms">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgBms">🔋 BMS 配置</legend>
<div class="ss"><div class="st" data-i18n="cfgBattParams">📋 电池参数</div>
<div class="fg"><label data-i18n="cfgChemistry">电池类型:</label><select id="bchem" onchange="onChemChange()"><option value="ncm"%BMS_CHEM_NCM% data-i18n="cfgChemNcm">三元锂 (NCM)</option><option value="lifepo4"%BMS_CHEM_LFP% data-i18n="cfgChemLfp">磷酸铁锂 (LiFePO4)</option></select></div>
<div class="fg"><label data-i18n="cfgCellCount">电池串数:</label><select id="bc"><option value="3"%BMS_CELL_COUNT_3% data-i18n="cfgCells3">3 串</option><option value="4"%BMS_CELL_COUNT_4% data-i18n="cfgCells4">4 串</option><option value="5"%BMS_CELL_COUNT_5% data-i18n="cfgCells5">5 串</option></select></div>
<div class="fg"><label data-i18n="cfgNominalCap">标称容量:</label><input type="number" id="bn" value="%BMS_NOMINAL_CAPACITY%" min="100" max="50000" step="100"><span class="u">mAh</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgVoltProtection">⚡ 电压保护</div>
<div class="fg"><label data-i18n="cfgOvThreshold">过压阈值:</label><input type="number" id="bo" value="%BMS_CELL_OV%" step="10"><span class="u">mV</span></div>
<div class="fg"><label data-i18n="cfgUvThreshold">欠压阈值:</label><input type="number" id="bu" value="%BMS_CELL_UV%" step="10"><span class="u">mV</span></div>
<div class="fg"><label data-i18n="cfgOvRecover">过压恢复:</label><input type="number" id="bor" value="%BMS_CELL_OV_RECOVER%" step="10"><span class="u">mV</span></div>
<div class="fg"><label data-i18n="cfgUvRecover">欠压恢复:</label><input type="number" id="bur" value="%BMS_CELL_UV_RECOVER%" step="10"><span class="u">mV</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgCurrProtection">🔌 电流保护</div>
<div class="fg"><label data-i18n="cfgMaxCharge">最大充电电流:</label><input type="number" id="bmc" value="%BMS_MAX_CHARGE%" min="100" max="10000" step="100"><span class="u">mA</span></div>
<div class="fg"><label data-i18n="cfgMaxDischarge">最大放电电流:</label><input type="number" id="bmd" value="%BMS_MAX_DISCHARGE%" min="100" max="20000" step="100"><span class="u">mA</span></div>
<div class="fg"><label data-i18n="cfgShortCircuit">短路阈值:</label><input type="number" id="bsc" value="%BMS_SHORT_CIRCUIT%" min="1000" max="30000" step="500"><span class="u">mA</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgTempProtection">🌡️ 温度保护</div>
<div class="fg"><label data-i18n="cfgOverheatTemp">关闭充放电温度:</label><input type="number" id="both" value="%BMS_OVERHEAT_THRESHOLD%" min="50" max="80" step="1"><span class="u">°C</span><span class="hint" data-i18n="cfgOverheatHint">超过此温度将关闭充放电，需手动重启恢复</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgBalConfig">⚖️ 均衡配置</div>
<div class="fg"><label data-i18n="cfgBalancing">电池均衡:</label><label class="cl"><input type="checkbox" id="bbe" %BMS_BALANCING_CHECKED%><span style="margin-left:8px" data-i18n="cfgEnabled">启用</span></label></div>
<div class="fg"><label data-i18n="cfgBalDiff">均衡压差:</label><input type="number" id="bbd" value="%BMS_BALANCING_DIFF%" min="5" max="100" step="5"><span class="u">mV</span></div>
<div class="hint" data-i18n="cfgBalLfpTip" style="margin-top:4px;font-size:12px;color:#888">磷酸铁锂电池仅在充电末端(单体>3.4V)执行均衡，平台区均衡无意义，属预期行为。</div>
</div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-windows">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgWindows">⏰ 充电时间窗口</legend>
<p style="color:#aaa;margin-bottom:10px;font-size:12px" data-i18n="cfgWindowDesc">配置允许充电的时间段。bit0=周日，bit1=周一 ... bit6=周六</p>
<div id="wc"></div>
<div style="margin-top:10px"><button type="button" class="btn btn-d" onclick="addW()" id="ab" data-i18n="cfgAddWindow">添加窗口</button></div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-power">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgPower">⚡ 电源管理</legend>
<div class="ss"><div class="st" data-i18n="cfgChargeConfig">🔌 充电配置</div>
<div class="fg"><label data-i18n="cfgMaxCharge">最大充电电流:</label><input type="number" id="pmc" value="%POWER_MAX_CHARGE%" min="100" max="10000" step="100"><span class="u">mA</span></div>
<div class="fg"><label data-i18n="cfgChargeVoltage">充电电压:</label><input type="number" id="pcv" value="%POWER_CHARGE_VOLTAGE%" min="10000" max="25000" step="100"><span class="u">mV</span></div>
<div class="fg"><label data-i18n="cfgChargeSocStart">启动 SOC:</label><input type="number" id="pcs" value="%POWER_CHARGE_SOC_START%" min="0" max="90" step="5"><span class="u">%</span></div>
<div class="fg"><label data-i18n="cfgChargeSocStop">停止 SOC:</label><input type="number" id="pcp" value="%POWER_CHARGE_SOC_STOP%" min="50" max="100" step="5"><span class="u">%</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgDischargeConfig">🔋 放电配置</div>
<div class="fg"><label data-i18n="cfgMaxDischarge">最大放电电流:</label><input type="number" id="pmd" value="%POWER_MAX_DISCHARGE%" min="100" max="20000" step="100"><span class="u">mA</span></div>
<div class="fg"><label data-i18n="cfgDischargeSocStop">停止 SOC:</label><input type="number" id="pds" value="%POWER_DISCHARGE_SOC_STOP%" min="0" max="30" step="5"><span class="u">%</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgHybridSection">🔄 混合供电</div>
<div class="fg"><label data-i18n="cfgHybridPower">混合供电:</label><label class="cl"><input type="checkbox" id="phe" %POWER_HYBRID_CHECKED%><span style="margin-left:8px" data-i18n="cfgEnabled">启用</span></label></div>
<div class="fg"><label data-i18n="cfgVsysMin">最小系统电压:</label><input type="number" id="pvsm" value="%POWER_VSYS_MIN%" min="5888" max="16128" step="256"><span class="u">mV</span><span class="hint" data-i18n="cfgVsysMinHint">仅 BQ24800 芯片有效，步进 256mV，默认 8960mV</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgProtConfig">🛡️ 保护配置</div>
<div class="fg"><label data-i18n="cfgAcInputCurrent">AC输入电流:</label><input type="number" id="poc" value="%POWER_OVER_CURRENT%" min="500" max="20000" step="100"><span class="u">mA</span><span class="hint" data-i18n="cfgAcInputHint">芯片配置最大值为8064，超过此值都会限制到8064</span></div>
<div class="fg"><label data-i18n="cfgOverTempThreshold">过温阈值:</label><input type="number" id="pot" value="%POWER_OVER_TEMP%" min="40" max="100" step="0.5"><span class="u">°C</span></div>
</div>
<div class="ss"><div class="st" data-i18n="cfgChargeTempLimit">🌡️ 充电温度限制</div>
<div class="fg"><label data-i18n="cfgChargeTempHigh">充电最高温:</label><input type="number" id="pth" value="%POWER_CHARGE_TEMP_HIGH%" min="30" max="60" step="0.5"><span class="u">°C</span></div>
<div class="fg"><label data-i18n="cfgChargeTempLow">充电最低温:</label><input type="number" id="ptl" value="%POWER_CHARGE_TEMP_LOW%" min="-20" max="10" step="0.5"><span class="u">°C</span></div>
</div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-auth">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgAuth">🔐 访问账户</legend>
<p style="color:#aaa;font-size:12px;margin-bottom:8px" data-i18n="cfgAuthDesc">修改登录设备管理页面的用户名和密码，修改后需重新登录。</p>
<div class="fg"><label data-i18n="cfgAuthUser">用户名:</label><input type="text" id="auth_user" maxlength="32" autocomplete="username"></div>
<div class="fg"><label data-i18n="cfgAuthPass">新密码:</label><input type="password" id="auth_pass" maxlength="64" placeholder="8-64" autocomplete="new-password"></div>
<div class="fg"><label data-i18n="cfgAuthPass2">确认密码:</label><input type="password" id="auth_pass2" maxlength="64" autocomplete="new-password"></div>
<div class="fg"><button type="button" class="btn" style="font-size:12px;padding:4px 12px" onclick="changeAuth()" data-i18n="cfgAuthSave">修改账户</button><span id="authStatus" style="font-size:12px;color:#999;margin-left:10px"></span></div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-calibration">
<fieldset class="fs">
<legend class="lg" data-i18n="cfgCalibration">📐 ADC 校准系数</legend>
<p style="color:#666;font-size:13px;margin:8px 0;line-height:1.6" data-i18n="cfgCalDesc">校正系数范围：50-255（表示 0.50x - 2.55x），100 = 1.00x（无校正）。修改后点击"保存校准"立即生效。</p>
<div id="calContainer" style="display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px;margin-top:12px"></div>
<div class="fa" style="margin-top:16px;display:flex;align-items:center;gap:12px">
<button type="button" class="btn" onclick="saveCalibration()" data-i18n="cfgSaveCal">保存校准</button>
<span id="calStatus" style="font-size:14px;color:#999"></span>
</div>
</fieldset>
<fieldset class="fs" style="border:2px solid #faad14;border-radius:8px;margin-top:16px">
<legend class="lg" style="color:#faad14" data-i18n="cfgBatteryReset">🔄 电池数据重置</legend>
<p style="color:#666;font-size:13px;margin:8px 0;line-height:1.6" data-i18n="cfgBatteryResetDesc">更换新电池后，重置 BMS 中记录的电池健康度(SOH)、循环次数、均衡统计数据。重置后系统将从开路电压重新初始化 SOC。</p>
<button type="button" class="btn" style="background:#faad14;border-color:#faad14" onclick="resetBatteryData()" data-i18n="cfgResetBtn">重置电池数据</button>
<span id="resetBmsStatus" style="font-size:14px;color:#999;margin-left:12px"></span>
</fieldset>
</div>

<div class="pnl" id="p-cfg-shipping">
<fieldset class="fs" style="border:2px solid #ff4d4f;border-radius:8px">
<legend class="lg" style="color:#ff4d4f" data-i18n="cfgShippingTitle">📦 运输以及存储模式</legend>
<p style="color:#666;font-size:13px;margin:8px 0;line-height:1.6" data-i18n="cfgShippingDesc">此模式会让bq76920电池管理芯片进入运输模式，也就是睡眠模式，将不会响应任何指令，一旦进入此模式，需要点按对应的硬件开关才能启用电池，此模式推荐运输或者长时间不使用情况下再执行。</p>
<button type="button" class="btn" style="background:#ff4d4f;border-color:#ff4d4f" onclick="enterShipMode()" data-i18n="cfgEnterMode">进入此模式</button>
</fieldset>
</div>

<div class="fa" id="cfgSaveBar">
<button type="button" class="btn" id="cfgSaveBtn" onclick="save()" data-i18n="cfgSave">保存配置</button>
</div>
<div class="nt" id="cfgSaveNote"><strong data-i18n="cfgSaveNote">⚠️ 注意：</strong><span data-i18n="cfgSaveNoteText">事关安全，保护参数请谨慎设置。</span></div>
</div>

<!-- ===== 面板：固件升级 (OTA) ===== -->
<div class="pnl" id="p-ota">
<div class="grid">
<div class="card"><div class="card-t" data-i18n="otaDeviceInfo">设备信息</div>
<div class="row"><span class="lb" data-i18n="otaChip">芯片型号</span><span class="vl">ESP32-S3</span></div>
<div class="row"><span class="lb" data-i18n="otaFirmware">当前固件</span><span class="vl">%FIRMWARE_VERSION%</span></div>
<div class="row"><span class="lb" data-i18n="otaSpace">可用空间</span><span class="vl">%FREE_SKETCH_SPACE% KB</span></div>
<div class="row"><span class="lb" data-i18n="otaFlash">Flash 大小</span><span class="vl">%FLASH_SIZE% MB</span></div>
</div>
<div class="card"><div class="card-t" data-i18n="otaNotes">升级须知</div>
<ul style="font-size:13px;color:#666;margin:8px 0;padding-left:20px">
<li data-i18n="otaNote1">仅支持 .bin 格式固件文件</li>
<li data-i18n="otaNote2">升级过程中请勿断电或重启</li>
<li data-i18n="otaNote3">升级约需 1-2 分钟</li>
<li data-i18n="otaNote4">升级完成后设备自动重启</li>
</ul>
</div>
</div>
<div class="card" style="margin-top:16px"><div class="card-t" data-i18n="otaFirmwareUpload">固件上传</div>
<div class="ua" id="upArea">
<div class="ic">&#8679;</div>
<label for="fwFile" data-i18n="otaSelectFile">点击选择固件文件或拖拽到此处</label>
<input type="file" id="fwFile" accept=".bin" onchange="selFile(this)" style="display:none">
<div class="fn" id="fName"></div>
</div>
<div class="prg" id="prgC"><div class="prg-bar"><div class="prg-fill" id="prgF">0%</div></div></div>
<div class="oms" id="stMsg"></div>
<button class="btn ota-btn" id="upBtn" onclick="upload()" disabled data-i18n="otaStartUpgrade">开始升级</button>
</div>

</div>
</div>

<!-- ===== 配置向导页面 (手机适配) ===== -->
<div class="wz-container" id="wz-page" style="display:none">
<div class="wz-header">
<div style="text-align:right;margin-bottom:8px"><select id="wz-lang" onchange="switchLang(this.value)" style="padding:4px 8px;border-radius:4px;border:1px solid #d9d9d9;font-size:13px"><option value="zh">中文</option><option value="en">English</option></select></div>
<h2 data-i18n="wzTitle">🔧 初始配置</h2>
<p data-i18n="wzSubtitle">欢迎！请完成以下步骤来配置您的设备</p>
</div>

<div class="wz-progress">
<div class="wz-step active" id="wz-step-0"><div class="wz-step-circle">1</div><div class="wz-step-label" data-i18n="wzStepWifi">WiFi</div></div>
<div class="wz-step" id="wz-step-1"><div class="wz-step-circle">2</div><div class="wz-step-label" data-i18n="wzStepNetwork">网络</div></div>
<div class="wz-step" id="wz-step-2"><div class="wz-step-circle">3</div><div class="wz-step-label" data-i18n="wzStepBattery">电池</div></div>
<div class="wz-step" id="wz-step-3"><div class="wz-step-circle">4</div><div class="wz-step-label" data-i18n="wzStepHardware">硬件</div></div>
<div class="wz-step" id="wz-step-4"><div class="wz-step-circle">5</div><div class="wz-step-label" data-i18n="wzStepComplete">完成</div></div>
</div>

<!-- 步骤 1: WiFi 设置 + 管理账户 -->
<div class="wz-card" id="wz-card-0">
<div class="wz-card-title" data-i18n="wzWifiTitle">📶 WiFi 设置</div>
<div class="wz-info" data-i18n="wzWifiDesc">连接到您的家庭或办公室 WiFi 网络</div>
<div class="wz-field">
<label data-i18n="wzWifiSsid">WiFi 名称 (SSID)</label>
<input type="text" id="wz-wifi-ssid" placeholder="输入 WiFi 名称" autocomplete="off">
</div>
<div class="wz-field">
<label data-i18n="wzWifiPass">WiFi 密码</label>
<input type="password" id="wz-wifi-pass" placeholder="输入 WiFi 密码" autocomplete="off">
</div>
<div class="wz-card-title" style="margin-top:20px" data-i18n="wzAuthTitle">🔐 管理账户（必填）</div>
<div class="wz-info" data-i18n="wzAuthDesc">用于登录设备管理页面，请妥善保管</div>
<div class="wz-field">
<label data-i18n="wzAuthUser">管理用户名</label>
<input type="text" id="wz-auth-user" maxlength="32" placeholder="admin" autocomplete="username">
</div>
<div class="wz-field">
<label data-i18n="wzAuthPass">管理密码（至少 8 位）</label>
<input type="password" id="wz-auth-pass" maxlength="64" autocomplete="new-password">
</div>
<div class="wz-field">
<label data-i18n="wzAuthPass2">确认管理密码</label>
<input type="password" id="wz-auth-pass2" maxlength="64" autocomplete="new-password">
</div>
</div>

<!-- 步骤 2: 网络配置 -->
<div class="wz-card" id="wz-card-1" style="display:none">
<div class="wz-card-title" data-i18n="wzNetworkTitle">🌐 网络配置</div>
<div class="wz-field">
<label data-i18n="wzIpMode">IP 地址获取方式</label>
<select id="wz-ip-mode" onchange="wzToggleIP()">
<option value="dhcp" data-i18n="wzDhcp">动态获取 (DHCP)</option>
<option value="static" data-i18n="wzStaticIp">固定 IP 地址</option>
</select>
</div>
<div id="wz-static-ip" style="display:none">
<div class="wz-field">
<label data-i18n="wzIpAddress">IP 地址</label>
<input type="text" id="wz-static-ip-addr" placeholder="192.168.1.100">
</div>
<div class="wz-field">
<label data-i18n="wzGateway">网关</label>
<input type="text" id="wz-static-gateway" placeholder="192.168.1.1">
</div>
<div class="wz-field">
<label data-i18n="wzSubnet">子网掩码</label>
<input type="text" id="wz-static-subnet" placeholder="255.255.255.0">
</div>
<div class="wz-field">
<label data-i18n="wzDns">DNS 服务器</label>
<input type="text" id="wz-static-dns" placeholder="8.8.8.8">
</div>
</div>
<div class="wz-field" style="margin-top:16px">
<label data-i18n="wzNtp">NTP 时间服务器</label>
<input type="text" id="wz-ntp-server" placeholder="ntp.aliyun.com" value="ntp.aliyun.com">
</div>
</div>

<!-- 步骤 3: 电池配置 -->
<div class="wz-card" id="wz-card-2" style="display:none">
<div class="wz-card-title" data-i18n="wzBatteryTitle">🔋 电池配置</div>
<div class="wz-info" data-i18n="wzBatteryDesc">请根据您的电池规格进行配置</div>
<div class="wz-field">
<label data-i18n="wzChemistry">电池类型</label>
<select id="wz-chemistry" onchange="wzChemChange()">
<option value="ncm" data-i18n="cfgChemNcm">三元锂 (NCM)</option>
<option value="lifepo4" data-i18n="cfgChemLfp">磷酸铁锂 (LiFePO4)</option>
</select>
</div>
<div class="wz-field">
<label data-i18n="wzCellCount">电池串数 (Cells)</label>
<select id="wz-cell-count">
<option value="3" data-i18n="wzCells3">3 串 (11.1V)</option>
<option value="4" data-i18n="wzCells4">4 串 (14.8V)</option>
<option value="5" data-i18n="wzCells5">5 串 (18.5V)</option>
</select>
</div>
<div class="wz-field">
<label data-i18n="wzCapacity">电池容量</label>
<input type="number" id="wz-capacity" placeholder="2000" min="100" max="50000">
</div>
<div class="wz-field">
<label data-i18n="wzChargeCur">最大充电电流 (mA)</label>
<input type="number" id="wz-charge-current" placeholder="2000" min="100" max="10000">
</div>
<div class="wz-field">
<label data-i18n="wzDischargeCur">最大放电电流 (mA)</label>
<input type="number" id="wz-discharge-current" placeholder="12000" min="100" max="20000">
</div>
<div class="wz-field">
<label data-i18n="wzOvThresh">过压保护阈值 (mV)</label>
<input type="number" id="wz-ov-threshold" placeholder="4210" min="3200" max="4500">
</div>
<div class="wz-field">
<label data-i18n="wzUvThresh">欠压保护阈值 (mV)</label>
<input type="number" id="wz-uv-threshold" placeholder="3000" min="2500" max="3500">
</div>
<div class="wz-field">
<div class="wz-checkbox">
<label data-i18n="wzBalancing">启用电池均衡</label>
<input type="checkbox" id="wz-balancing" checked>
</div>
</div>
</div>

<!-- 步骤 4: 硬件设置 -->
<div class="wz-card" id="wz-card-3" style="display:none">
<div class="wz-card-title" data-i18n="wzHardwareTitle">⚙️ 硬件设置</div>
<div class="wz-field">
<div class="wz-checkbox">
<label data-i18n="wzBuzzer">启用蜂鸣器</label>
<input type="checkbox" id="wz-buzzer" checked>
</div>
</div>
<div class="wz-field">
<label data-i18n="wzBuzzerVol">蜂鸣器音量</label>
<input type="range" id="wz-volume" min="0" max="100" value="100" oninput="document.getElementById('wz-vol-val').textContent=this.value+'%'">
<div style="text-align:right;font-size:12px;color:#888;margin-top:4px"><span id="wz-vol-val">100%</span></div>
</div>
<div class="wz-field">
<label data-i18n="wzLedBrightness">LED 亮度</label>
<input type="range" id="wz-brightness" min="0" max="100" value="30" oninput="document.getElementById('wz-br-val').textContent=this.value+'%'">
<div style="text-align:right;font-size:12px;color:#888;margin-top:4px"><span id="wz-br-val">30%</span></div>
</div>
<div class="wz-field">
<div class="wz-checkbox">
<label data-i18n="wzHid">启用 HID 报告</label>
<input type="checkbox" id="wz-hid" checked>
</div>
</div>
<div class="wz-field">
<label data-i18n="wzHidMode">HID 电量模式</label>
<select id="wz-hid-mode">
<option value="0">mAh</option>
<option value="1">mWh</option>
<option value="2" data-i18n="wzHidPct">百分比 (%)(nas，linux选这个)</option>
</select>
</div>
</div>

<!-- 步骤 5: 完成 -->
<div class="wz-card" id="wz-card-4" style="display:none">
<div class="wz-success">
<div class="wz-success-icon">✅</div>
<h3 data-i18n="wzCompleteTitle">配置完成！</h3>
<p><span data-i18n="wzCompleteDesc">配置已保存，设备即将重启</span><br><span data-i18n="wzCompleteDesc2">重启后请访问管理页面查看实时监控</span></p>
<div id="wz-reboot-count" style="margin-top:16px;font-size:18px;color:#1677ff;font-weight:600">5</div>
</div>
</div>

<div class="wz-navigate" id="wz-nav">
<button class="wz-btn wz-btn-prev" id="wz-prev" onclick="wzPrev()" disabled data-i18n="wzPrev">上一步</button>
<button class="wz-btn wz-btn-next" id="wz-next" onclick="wzNext()" data-i18n="wzNext">下一步</button>
</div>
</div>
</div>
<div class="foot"><span id="updT">--</span></div>
</div>
</div>
<script id="dynamic-js"></script>
</body>
</html>
)rawliteral";

#endif // PAGE_TEMPLATES_H