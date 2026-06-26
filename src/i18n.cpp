#include "i18n.h"
#include "i18n_strings_zh.h"
#include "i18n_strings_en.h"
#include <Preferences.h>

// =============================================================================
// 静态成员初始化
// =============================================================================
Language I18n::currentLang_ = LANG_ZH;

// =============================================================================
// NVS 读写
// =============================================================================
void I18n::loadLanguage() {
    Preferences prefs;
    prefs.begin("ups_config", true);  // 只读
    uint8_t lang = prefs.getUChar("lang", LANG_ZH);
    prefs.end();
    currentLang_ = (lang < LANG_COUNT) ? (Language)lang : LANG_ZH;
}

void I18n::setLanguage(Language lang) {
    if (lang >= LANG_COUNT) return;
    currentLang_ = lang;
    Preferences prefs;
    prefs.begin("ups_config", false);  // 读写
    prefs.putUChar("lang", (uint8_t)lang);
    prefs.end();
}

Language I18n::getCurrentLang() {
    return currentLang_;
}

const char* I18n::getLangCode() {
    return (currentLang_ == LANG_EN) ? "en" : "zh";
}

const char* I18n::get(StrId id) {
    if (id >= STR_COUNT) return "";
    if (currentLang_ == LANG_EN) {
        return strings_en_[id];
    }
    return strings_zh_[id];
}
