#pragma once

#include <string>
#include <switch.h>
#include <unordered_map>
#include <memory>

// 全局字符串变量，对应json中的键
// Global string variable corresponding to a key in the JSON
extern std::string loading_text;
extern std::string button_back;
extern std::string button_stop;
extern std::string software_title;
extern std::string software_title_loading;
extern std::string delete_title_loading;
extern std::string no_app_found;
extern std::string total_selected;
extern std::string plus_sign;
extern std::string delete_selected_count;
extern std::string uninstalled_all_app;
extern std::string cumulative_released;
extern std::string pending_total;
extern std::string space_releasing;
extern std::string button_uninstalled;
extern std::string button_remove;
extern std::string space_available;
extern std::string storage_nand;
extern std::string storage_sd;
extern std::string selected_count;
extern std::string button_select;
extern std::string button_exit;
extern std::string button_delete_selected;
extern std::string button_deselect_all;
extern std::string button_select_all;
extern std::string button_invert_select;
extern std::string sort_alpha_az;
extern std::string sort_size_bigsmall;
extern std::string corrupted_install;
extern std::string system_memory;
extern std::string micro_sd_card;

namespace tj {

class LangManager {

public:
    // 获取单例实例
    // Get the singleton instance
    static LangManager& getInstance();

    // 加载指定语言的JSON文件
    // Load the JSON file for the specified language
    bool loadLanguage(const std::string& langCode);

    // 自动检测并加载系统语言
    // Automatically detect and load the system language
    bool loadSystemLanguage();

    // 获取当前系统语言代号
    // Get the current system language code
    int getCurrentLanguage() const;

    // 加载系统语言
    // Load the system language
    bool loadDefaultLanguage();

    // 禁止拷贝和移动
    // Disable copy and move
    LangManager(const LangManager&) = delete;
    LangManager& operator=(const LangManager&) = delete;
    LangManager(LangManager&&) = delete;
    LangManager& operator=(LangManager&&) = delete;

    

private:
    // 私有构造函数
    // Private constructor
    LangManager() = default;

    // 文本映射表
    // Text mapping table
    std::unordered_map<std::string, std::string> m_textMap;

    // 当前系统语言代号
    // Current system language code
    int m_currentLanguage; 

};

} // namespace tj