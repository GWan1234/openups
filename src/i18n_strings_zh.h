#ifndef I18N_STRINGS_ZH_H
#define I18N_STRINGS_ZH_H

#include "i18n.h"

// =============================================================================
// 中文字符串表
// =============================================================================

// 通用
static const char s_zh_0[] PROGMEM = "已启用";
static const char s_zh_1[] PROGMEM = "已禁用";

// addTip 时间格式
static const char s_zh_2[] PROGMEM = "[%d月%d日 %02d:%02d]";

// addTip 消息：状态切换
static const char s_zh_3[] PROGMEM = "BMS模块掉线，进入危急状态";
static const char s_zh_4[] PROGMEM = "电源芯片BQ24780S掉线，进入危急状态";
static const char s_zh_5[] PROGMEM = "BMS故障(%d)，进入危急状态";
static const char s_zh_6[] PROGMEM = "系统进入危急状态";
static const char s_zh_7[] PROGMEM = "电源故障(%d)，进入警告状态";
static const char s_zh_8[] PROGMEM = "过流保护触发，进入警告状态";
static const char s_zh_9[] PROGMEM = "过温保护触发，进入警告状态";
static const char s_zh_10[] PROGMEM = "BQ24780S寄存器配置不一致，进入警告状态";
static const char s_zh_11[] PROGMEM = "BQ76920寄存器配置不一致，进入警告状态";
static const char s_zh_12[] PROGMEM = "系统进入警告状态";

// addTip 消息：BMS 故障
static const char s_zh_13[] PROGMEM = "BMS过温保护：关闭充放电";
static const char s_zh_14[] PROGMEM = "BMS过压保护：关闭充电";
static const char s_zh_15[] PROGMEM = "BMS欠压保护：关闭放电";
static const char s_zh_16[] PROGMEM = "BMS过流保护：关闭充放电";
static const char s_zh_17[] PROGMEM = "BMS短路保护：紧急关机";
static const char s_zh_18[] PROGMEM = "BMS芯片错误";
static const char s_zh_19[] PROGMEM = "BMS被动关机：关闭充放电";
static const char s_zh_20[] PROGMEM = "BMS未知故障(%d)";

// addTip 消息：寄存器不一致
static const char s_zh_21[] PROGMEM = "不一致:寄存器=%dmA,配置=%dmA";
static const char s_zh_22[] PROGMEM = "不一致:寄存器=%dmV,配置=%dmV";

// addTip 消息：电池操作
static const char s_zh_23[] PROGMEM = "电池数据已重置 (SOH/循环/均衡)";

// addTip 消息：充放电事件
static const char s_zh_24[] PROGMEM = "正在充电，SOC:%.1f%%，充电电流：%dmA，充电电压：%dmV";
static const char s_zh_25[] PROGMEM = "充电已停止，SOC:%.1f%%";
static const char s_zh_26[] PROGMEM = "电池均衡启动，SOC:%.1f%%，均衡电芯：%s";
static const char s_zh_27[] PROGMEM = "电池均衡停止，SOC:%.1f%%";

// MQTT 实体名称
static const char s_zh_28[] PROGMEM = "电池电量";
static const char s_zh_29[] PROGMEM = "电池健康度";
static const char s_zh_30[] PROGMEM = "电池电压";
static const char s_zh_31[] PROGMEM = "电池电流";
static const char s_zh_32[] PROGMEM = "电池温度";
static const char s_zh_33[] PROGMEM = "电池剩余容量";
static const char s_zh_34[] PROGMEM = "电池循环次数";
static const char s_zh_35[] PROGMEM = "系统自消耗电流";
static const char s_zh_36[] PROGMEM = "电芯%d电压";
static const char s_zh_37[] PROGMEM = "电芯最低电压";
static const char s_zh_38[] PROGMEM = "电芯最高电压";
static const char s_zh_39[] PROGMEM = "电芯平均电压";
static const char s_zh_40[] PROGMEM = "输入电压";
static const char s_zh_41[] PROGMEM = "输入电流";
static const char s_zh_42[] PROGMEM = "输出功率";
static const char s_zh_43[] PROGMEM = "电池电压(adc)";
static const char s_zh_44[] PROGMEM = "电池电流(adc)";
static const char s_zh_45[] PROGMEM = "AC电源";
static const char s_zh_46[] PROGMEM = "充电器使能";
static const char s_zh_47[] PROGMEM = "均衡状态";
static const char s_zh_48[] PROGMEM = "WiFi连接";
static const char s_zh_49[] PROGMEM = "BMS故障";
static const char s_zh_50[] PROGMEM = "电源故障";
static const char s_zh_51[] PROGMEM = "紧急关机";
static const char s_zh_52[] PROGMEM = "过流保护";
static const char s_zh_53[] PROGMEM = "过温保护";
static const char s_zh_54[] PROGMEM = "短路保护";
static const char s_zh_55[] PROGMEM = "系统运行时间";
static const char s_zh_56[] PROGMEM = "板温";
static const char s_zh_57[] PROGMEM = "环境温度";
static const char s_zh_58[] PROGMEM = "SHTC3温度";
static const char s_zh_59[] PROGMEM = "SHTC3湿度";
static const char s_zh_60[] PROGMEM = "WiFi信号强度";
static const char s_zh_61[] PROGMEM = "固件版本";
static const char s_zh_62[] PROGMEM = "供电模式";
static const char s_zh_63[] PROGMEM = "整体状态";
static const char s_zh_64[] PROGMEM = "WiFi SSID";
static const char s_zh_65[] PROGMEM = "LED亮度";
static const char s_zh_66[] PROGMEM = "蜂鸣器音量";
static const char s_zh_67[] PROGMEM = "HID报告模式";
static const char s_zh_68[] PROGMEM = "电芯%d内阻";
static const char s_zh_69[] PROGMEM = "内阻采样次数";

// Webhook：API 响应
static const char s_zh_70[] PROGMEM = "Webhook 模块未初始化";
static const char s_zh_71[] PROGMEM = "缺少请求体";
static const char s_zh_72[] PROGMEM = "JSON 解析失败";
static const char s_zh_73[] PROGMEM = "无效端点索引";
static const char s_zh_74[] PROGMEM = "配置验证失败";
static const char s_zh_75[] PROGMEM = "配置保存失败";
static const char s_zh_76[] PROGMEM = "Webhook 配置已保存";

// Webhook：测试发送
static const char s_zh_77[] PROGMEM = "WiFi 未连接";
static const char s_zh_78[] PROGMEM = "端点未启用";
static const char s_zh_79[] PROGMEM = "Webhook 测试";
static const char s_zh_80[] PROGMEM = "这是一条 Webhook 测试消息，来自 ESP32-S3 UPS";

// Webhook：触发器消息模板
static const char s_zh_81[] PROGMEM = "%s 当前 %.1f 超过阈值 %.1f";
static const char s_zh_82[] PROGMEM = "%s 当前 %.1f 低于阈值 %.1f";
static const char s_zh_83[] PROGMEM = "%s 已恢复，当前 %.1f";
static const char s_zh_84[] PROGMEM = "%s 已恢复: %s";

// Webhook：状态字符串
static const char s_zh_85[] PROGMEM = "AC 已连接";
static const char s_zh_86[] PROGMEM = "AC 已断开";
static const char s_zh_87[] PROGMEM = "充电已开启";
static const char s_zh_88[] PROGMEM = "充电已关闭";
static const char s_zh_89[] PROGMEM = "BMS 故障";
static const char s_zh_90[] PROGMEM = "BMS 正常";
static const char s_zh_91[] PROGMEM = "电源故障";
static const char s_zh_92[] PROGMEM = "电源正常";
static const char s_zh_93[] PROGMEM = "初始化";
static const char s_zh_94[] PROGMEM = "正常";
static const char s_zh_95[] PROGMEM = "警告";
static const char s_zh_96[] PROGMEM = "严重";
static const char s_zh_97[] PROGMEM = "AC 供电";
static const char s_zh_98[] PROGMEM = "电池供电";
static const char s_zh_99[] PROGMEM = "混合供电";
static const char s_zh_100[] PROGMEM = "充电中";
static const char s_zh_101[] PROGMEM = "紧急关机";
static const char s_zh_102[] PROGMEM = "均衡中";
static const char s_zh_103[] PROGMEM = "均衡停止";
static const char s_zh_104[] PROGMEM = "未知";

// Webhook：模板变量
static const char s_zh_105[] PROGMEM = "连接";
static const char s_zh_106[] PROGMEM = "断开";

// Webhook：配置验证失败原因
static const char s_zh_107[] PROGMEM = "配置版本不匹配";
static const char s_zh_108[] PROGMEM = "端点数量超限";
static const char s_zh_109[] PROGMEM = "端点 %u URL 不能为空";
static const char s_zh_110[] PROGMEM = "端点 %u 请求方式无效";
static const char s_zh_111[] PROGMEM = "端点 %u 冷却时间不能小于 10 秒";
static const char s_zh_112[] PROGMEM = "端点 %u 触发器数量超限";
static const char s_zh_113[] PROGMEM = "端点 %u 触发器类型无效";
static const char s_zh_114[] PROGMEM = "端点 %u 比较运算符无效";
static const char s_zh_115[] PROGMEM = "端点 %u 值触发只能用大于/小于";
static const char s_zh_116[] PROGMEM = "端点 %u 状态触发只能用等于/变化";
static const char s_zh_117[] PROGMEM = "端点 %u 监测值无效";
static const char s_zh_118[] PROGMEM = "端点 %u 监测状态无效";
static const char s_zh_119[] PROGMEM = "端点 %u 告警级别无效";

// 指针数组（按 StrId 顺序）
const char* const I18n::strings_zh_[] = {
    s_zh_0,  s_zh_1,  s_zh_2,  s_zh_3,  s_zh_4,  s_zh_5,  s_zh_6,  s_zh_7,
    s_zh_8,  s_zh_9,  s_zh_10, s_zh_11, s_zh_12, s_zh_13, s_zh_14, s_zh_15,
    s_zh_16, s_zh_17, s_zh_18, s_zh_19, s_zh_20, s_zh_21, s_zh_22, s_zh_23,
    s_zh_24, s_zh_25, s_zh_26, s_zh_27, s_zh_28, s_zh_29, s_zh_30, s_zh_31,
    s_zh_32, s_zh_33, s_zh_34, s_zh_35, s_zh_36, s_zh_37, s_zh_38, s_zh_39,
    s_zh_40, s_zh_41, s_zh_42, s_zh_43, s_zh_44, s_zh_45, s_zh_46, s_zh_47,
    s_zh_48, s_zh_49, s_zh_50, s_zh_51, s_zh_52, s_zh_53, s_zh_54, s_zh_55,
    s_zh_56, s_zh_57, s_zh_58, s_zh_59, s_zh_60, s_zh_61, s_zh_62, s_zh_63,
    s_zh_64, s_zh_65, s_zh_66, s_zh_67, s_zh_68, s_zh_69,
    s_zh_70, s_zh_71, s_zh_72, s_zh_73, s_zh_74, s_zh_75, s_zh_76, s_zh_77,
    s_zh_78, s_zh_79, s_zh_80, s_zh_81, s_zh_82, s_zh_83, s_zh_84, s_zh_85,
    s_zh_86, s_zh_87, s_zh_88, s_zh_89, s_zh_90, s_zh_91, s_zh_92, s_zh_93,
    s_zh_94, s_zh_95, s_zh_96, s_zh_97, s_zh_98, s_zh_99, s_zh_100, s_zh_101,
    s_zh_102, s_zh_103, s_zh_104, s_zh_105, s_zh_106, s_zh_107, s_zh_108, s_zh_109,
    s_zh_110, s_zh_111, s_zh_112, s_zh_113, s_zh_114, s_zh_115, s_zh_116, s_zh_117,
    s_zh_118, s_zh_119,
};

#endif
