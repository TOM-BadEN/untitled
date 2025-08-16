#include "lang_manager.hpp"
#include <fstream>
#include <cstring>
#include <string>
#include <functional>

// 定义全局字符串变量
// Global string variables
std::string loading_text = "Scanning, please wait...";
std::string button_back = "Back";
std::string button_stop = "Stop";
std::string software_title = "Game Uninstall Tool";
std::string software_title_loading = "%s Scanning %lu/%lu";
std::string delete_title_loading = "%s Uninstalling %lu/%lu";
std::string no_app_found = "No apps found. Press B to exit.";
std::string total_selected = "Selected Total";
std::string plus_sign = "+ ";
std::string uninstalled_all_app = "All apps removed. Press B to return.";
std::string delete_selected_count = "Selected %d items";
std::string pending_total = "Total Pending";
std::string space_releasing = "Space Releasing";
std::string cumulative_released = "Total Freed";
std::string button_uninstalled = "Uninstall";
std::string space_available = "Space Available";
std::string storage_nand = "Nand";
std::string storage_sd = "Sd";
std::string selected_count = "Selected %lu / %lu";
std::string button_select = "Select";
std::string button_remove = "Remove";
std::string button_exit = "Exit";
std::string button_delete_selected = "Uninstall";
std::string button_deselect_all = "Uninstall";
std::string button_select_all = "Select All";
std::string button_invert_select = "Invert";
std::string sort_alpha_az = "Sort: Name";
std::string sort_size_bigsmall = "Sort: Size";
std::string corrupted_install = "Corrupted";
std::string system_memory = "System memory";
std::string micro_sd_card = "microSD card";

namespace tj {

LangManager& LangManager::getInstance() {
    static LangManager instance;
    return instance;
}

// 改进的JSON解析函数，能够处理空格和换行符
// Improved JSON parsing function that can handle whitespace and line breaks
bool parseSimpleJSON(const std::string& jsonStr, std::unordered_map<std::string, std::string>& textMap) {
    textMap.clear();
    size_t pos = jsonStr.find('{'); // 定位到JSON对象开始
    if (pos == std::string::npos) return false;
    pos++;

    // 跳过空格和换行符的辅助函数
    // Helper function to skip whitespace and line breaks
    auto skipWhitespace = [&](size_t start) {
        while (start < jsonStr.size() && (jsonStr[start] == ' ' || jsonStr[start] == '\t' || jsonStr[start] == '\n' || jsonStr[start] == '\r')) {
            start++;
        }
        return start;
    };

    // 查找所有键值对
    // Find all key-value pairs
    while (pos < jsonStr.size()) {
        pos = skipWhitespace(pos);
        if (pos >= jsonStr.size() || jsonStr[pos] == '}') break;

        // 查找键的开始（双引号）
        // Find the start of the key (double quote)
        if (jsonStr[pos] != '"') {
            // 不是双引号，可能是格式错误，尝试找到下一个双引号
            // It's not a double quote. It might be a formatting error. Please try to find the next double quote.
            pos = jsonStr.find('"', pos);
            if (pos == std::string::npos || jsonStr[pos] != '"') {
                // 找不到双引号，解析失败
                // Can't find a double quote. Parsing failed.
                break;
            }
        }
        size_t keyStart = pos + 1;

        // 查找键的结束（双引号，考虑转义字符）
        // Find the end of the key (double quote, considering escape characters)
        bool inEscape = false;
        size_t keyEnd = keyStart;
        while (keyEnd < jsonStr.size()) {
            if (!inEscape && jsonStr[keyEnd] == '\\') {
                inEscape = true;
            } else if (!inEscape && jsonStr[keyEnd] == '"') {
                break;
            } else {
                inEscape = false;
            }
            keyEnd++;
        }
        if (keyEnd >= jsonStr.size()) break;

        // 提取键
        // Extract the key
        std::string key = jsonStr.substr(keyStart, keyEnd - keyStart);

        // 移动到冒号
        // Move to the colon
        pos = keyEnd + 1;
        pos = skipWhitespace(pos);
        if (pos >= jsonStr.size() || jsonStr[pos] != ':') {
            // 找不到冒号，跳过到下一个逗号或右括号
            // Can't find a colon. Skip to the next comma or right parenthesis.
            pos = jsonStr.find_first_of(",}", pos);
            if (pos < jsonStr.size() && jsonStr[pos] == ',') pos++;
            continue;
        }
        pos++;
        pos = skipWhitespace(pos);

        // 查找值的开始（双引号）
        // Find the start of the value (double quote)
        if (pos >= jsonStr.size() || jsonStr[pos] != '"') {
            // 找不到双引号，跳过到下一个逗号或右括号
            // Can't find a double quote. Skip to the next comma or right parenthesis.
            pos = jsonStr.find_first_of(",}", pos);
            if (pos < jsonStr.size() && jsonStr[pos] == ',') pos++;
            continue;
        }
        size_t valStart = pos + 1;

        // 查找值的结束（双引号，考虑转义字符）
        // Find the end of the value (double quote, considering escape characters)
        inEscape = false;
        size_t valEnd = valStart;
        while (valEnd < jsonStr.size()) {
            if (!inEscape && jsonStr[valEnd] == '\\') {
                inEscape = true;
            } else if (!inEscape && jsonStr[valEnd] == '"') {
                break;
            } else {
                inEscape = false;
            }
            valEnd++;
        }
        if (valEnd >= jsonStr.size()) break;

        // 提取值并处理转义字符
        // Extract the value and handle escape characters
        std::string value = jsonStr.substr(valStart, valEnd - valStart);
        std::string processedValue;
        for (size_t i = 0; i < value.size(); i++) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                i++;
                switch (value[i]) {
                    case 'n': processedValue += '\n'; break;
                    case 't': processedValue += '\t'; break;
                    case 'r': processedValue += '\r'; break;
                    case '"': processedValue += '"'; break;
                    case '\\': processedValue += '\\'; break;
                    default: processedValue += value[i]; break;
                }
            } else {
                processedValue += value[i];
            }
        }

        textMap[key] = processedValue;

        // 移动到下一个键值对
        // Move to the next key-value pair
        pos = valEnd + 1;
        pos = skipWhitespace(pos);
        if (pos < jsonStr.size() && jsonStr[pos] == ',') pos++;
    }

    return !textMap.empty();
}

bool LangManager::loadLanguage(const std::string& langCode) {
    // 清空现有文本映射
    // Clear existing text mappings
    m_textMap.clear();

    // 构建语言文件路径
    // Build the language file path
    std::string filePath = "romfs:/lang/" + langCode + ".json";

    // 尝试打开文件
    // Try to open the file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        // 如果指定语言文件不存在，尝试加载en.json
        // If the specified language file doesn't exist, try to load en.json
        filePath = "romfs:/lang/en.json";
        file.open(filePath);
        if (!file.is_open()) {
            return false;
        }
    }

    // 读取整个文件内容
    // Read the entire file content
    std::string jsonStr((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // 解析JSON
    // Parse the JSON
    bool parseSuccess = parseSimpleJSON(jsonStr, m_textMap);

    // 如果解析成功，更新全局变量
    // If the parsing is successful, update the global variables
    if (parseSuccess) {
        // 创建键与变量的映射关系
        // Create a mapping relationship between keys and variables
        std::vector<std::pair<std::string, std::reference_wrapper<std::string>>> textMappings = {
            {"loading_text", std::ref(loading_text)},
            {"button_back", std::ref(button_back)},
            {"button_stop", std::ref(button_stop)},
            {"software_title", std::ref(software_title)},
            {"software_title_loading", std::ref(software_title_loading)},
            {"delete_title_loading", std::ref(delete_title_loading)},
            {"no_app_found", std::ref(no_app_found)},
            {"total_selected", std::ref(total_selected)},
            {"plus_sign", std::ref(plus_sign)},
            {"delete_selected_count", std::ref(delete_selected_count)},
            {"space_releasing", std::ref(space_releasing)},
            {"button_uninstalled", std::ref(button_uninstalled)},
            {"uninstalled_all_app", std::ref(uninstalled_all_app)},
            {"cumulative_released", std::ref(cumulative_released)},
            {"pending_total", std::ref(pending_total)},
            {"space_available", std::ref(space_available)},
            {"storage_nand", std::ref(storage_nand)},
            {"storage_sd", std::ref(storage_sd)},
            {"selected_count", std::ref(selected_count)},
            {"button_select", std::ref(button_select)},
            {"button_remove", std::ref(button_remove)},
            {"button_exit", std::ref(button_exit)},
            {"button_delete_selected", std::ref(button_delete_selected)},
            {"button_deselect_all", std::ref(button_deselect_all)},
            {"button_select_all", std::ref(button_select_all)},
            {"button_invert_select", std::ref(button_invert_select)},
            {"sort_alpha_az", std::ref(sort_alpha_az)},
            {"sort_size_bigsmall", std::ref(sort_size_bigsmall)},
            {"corrupted_install", std::ref(corrupted_install)},
            {"system_memory", std::ref(system_memory)},
            {"micro_sd_card", std::ref(micro_sd_card)}
        };

        // 遍历映射表进行赋值 - 修改为更兼容的遍历方式
        // Traverse the mapping table to assign values - modify to a more compatible traversal way
        for (size_t i = 0; i < textMappings.size(); ++i) {
            const std::string& key = textMappings[i].first;
            std::reference_wrapper<std::string>& ref = textMappings[i].second;
            
            auto it = m_textMap.find(key);
            if (it != m_textMap.end()) {
                ref.get() = it->second;  // 显式使用get()获取引用 // Explicitly use get() to obtain a reference
                
                
            } 
        }
    }
    
    return parseSuccess;
}

int LangManager::getCurrentLanguage() const {


    return m_currentLanguage; 
}


bool LangManager::loadSystemLanguage() {
    u64 LanguageCode = 0;
    SetLanguage Language = SetLanguage_ENUS;
    Result rc = setInitialize();
    
    if (R_SUCCEEDED(rc)) {
        rc = setGetSystemLanguage(&LanguageCode);
        if (R_SUCCEEDED(rc)) {
            rc = setMakeLanguage(LanguageCode, &Language);
        }
        setExit(); // 确保关闭set服务 // Ensure that the set service is closed
    }
    
    if (R_SUCCEEDED(rc)) {
        // 根据系统语言加载对应的语言文件
        // Load the corresponding language file based on the system language
        m_currentLanguage = Language;
        switch(Language) {
            case 15: // 简体中文
                m_currentLanguage = 14;
                return loadLanguage("zh-Hans");
            case 16: // 繁体中文
                m_currentLanguage = 13;
                return loadLanguage("zh-Hant");
            case 0: // 日语
                m_currentLanguage = 2;
                return loadLanguage("ja");
            case 7: // 韩语
                m_currentLanguage = 12;
                return loadLanguage("ko");
            case 2: // 法语
                m_currentLanguage = 3;
                return loadLanguage("fr");
            case 3: // 德语
                m_currentLanguage = 4;  
                return loadLanguage("de");
            case 10: // 俄语
                m_currentLanguage = 11;
                return loadLanguage("ru");
            case 5: // 西班牙语
                m_currentLanguage = 5;
                return loadLanguage("es");
            case 9: // 葡萄牙语
                m_currentLanguage = 15;
                return loadLanguage("pt");
            case 4: // 意大利语
                m_currentLanguage = 7;
                return loadLanguage("it");
            case 8: // 荷兰语
                m_currentLanguage = 8;
                return loadLanguage("nl");
            default: // 默认英语
                m_currentLanguage = 0;
                return loadLanguage("en");
        }
    } else {
        // 如果获取系统语言失败，默认加载英语
        // If the system language cannot be obtained, load English by default
        return loadLanguage("en");
    }
}



} // namespace tj
