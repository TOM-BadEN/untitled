#pragma once

#include "nanovg/nanovg.h"
#include <cstdarg>
#include <array>
#include <variant>
#include <cstdio>

namespace tj::gfx {

enum class Colour {
    BLACK,
    LIGHT_BLACK,
    SILVER,
    DARK_GREY,
    GREY,
    WHITE,
    CYAN,
    TEAL,
    BLUE,
    LIGHT_BLUE,
    YELLOW,
    RED,
};

enum class Button {
    POWER = 0xE0B8,
    A = 0xE0E0,
    B = 0xE0E1,
    X = 0xE0E2,
    Y = 0xE0E3,
    L = 0xE0E4,
    R = 0xE0E5,
    ZL = 0xE0E6,
    ZR = 0xE0E7,
    SL = 0xE0E8,
    SR = 0xE0E9,
    UP = 0xE0EB,
    DOWN = 0xE0EC,
    LEFT = 0xE0ED,
    RIGHT = 0xE0EE,
    PLUS = 0xE0EF,
    MINUS = 0xE0F0,
    HOME = 0xE0F4,
    SCREENSHOT = 0xE0F5,
    LS = 0xE101,
    RS = 0xE102,
    L3 = 0xE104,
    R3 = 0xE105,
    SWITCH = 0xE121,
    DUALCONS = 0xE122,
    SETTINGS = 0xE130,
    NEWS = 0xE132,
    ESHOP = 0xE133,
    ALBUM = 0xE134,
    ALLAPPS = 0xE135,
    CONTROLLER = 0xE136,
};

NVGcolor getColour(Colour c);

void drawRect(NVGcontext*, float x, float y, float w, float h, Colour c);
void drawRect(NVGcontext*, float x, float y, float w, float h, const NVGcolor& c);
void drawRect(NVGcontext*, float x, float y, float w, float h, const NVGcolor&& c);
void drawRect(NVGcontext*, float x, float y, float w, float h, const NVGpaint& p);
void drawRect(NVGcontext*, float x, float y, float w, float h, const NVGpaint&& p);

void drawText(NVGcontext*, float x, float y, float size, const char* str, const char* end, int align, Colour c);
void drawText(NVGcontext*, float x, float y, float size, const char* str, const char* end, int align, const NVGcolor& c);
void drawText(NVGcontext*, float x, float y, float size, const char* str, const char* end, int align, const NVGcolor&& c);
void drawTextArgs(NVGcontext*, float x, float y, float size, int align, Colour c, const char* str, ...);

void drawButton(NVGcontext* vg, float x, float y, float size, Button button);

const char* getButton(Button button);

// 定义支持组合按钮的新pair类型
using pair2 = std::pair<std::variant<Button, std::pair<Button, Button>>, const char*>;

// 支持单独设置按钮颜色的结构体
struct pair2_colored {
    std::variant<Button, std::pair<Button, Button>> button;
    const char* text;
    Colour color;
};

// 添加支持文本换行的函数声明
void drawTextBox(NVGcontext* vg, float x, float y, float width, float size, const char* str, const char* end, int align, Colour c);
void drawTextBox(NVGcontext* vg, float x, float y, float width, float size, float lineSpacing, const char* str, const char* end, int align, Colour c);
void drawTextBoxCentered(NVGcontext* vg, float x, float y, float width, float height, float size, float lineSpacing, const char* str, const char* end, Colour c);

// 为组合按钮创建便捷构造函数
inline pair2 make_pair2(Button button, const char* text) {
    return pair2{std::variant<Button, std::pair<Button, Button>>{button}, text};
}

inline pair2 make_pair2(Button first_button, Button second_button, const char* text) {
    return pair2{std::variant<Button, std::pair<Button, Button>>{std::pair<Button, Button>{first_button, second_button}}, text};
}

// 为支持颜色的按钮创建便捷构造函数
inline pair2_colored make_pair2_colored(Button button, const char* text, Colour color) {
    return pair2_colored{std::variant<Button, std::pair<Button, Button>>{button}, text, color};
}

inline pair2_colored make_pair2_colored(Button first_button, Button second_button, const char* text, Colour color) {
    return pair2_colored{std::variant<Button, std::pair<Button, Button>>{std::pair<Button, Button>{first_button, second_button}}, text, color};
}

// 新增支持组合按钮绘制的函数
void drawButtons2(NVGcontext* vg, Colour color = Colour::WHITE, std::same_as<pair2> auto ...args) {
    const std::array list = {args...};
    nvgBeginPath(vg);
    nvgFontSize(vg, 24.f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, getColour(color));

    float x = 1220.f;
    const float y = 675.f;
    float bounds[4]{};

    for (const auto& [button, text] : list) {
        nvgFontSize(vg, 20.f);
        nvgTextBounds(vg, x, y, text, nullptr, bounds);
        auto len = bounds[2] - bounds[0];
        nvgText(vg, x, y, text, nullptr);

        x -= len + 10.f;
        
        // 处理单个按钮或组合按钮
        nvgFontSize(vg, 30.f);
        if (std::holds_alternative<Button>(button)) {
            // 单个按钮
            const Button single_button = std::get<Button>(button);
            nvgTextBounds(vg, x, y - 7.f, getButton(single_button), nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, getButton(single_button), nullptr);
            x -= len + 34.f;
        } else {
            // 组合按钮
            const auto& [first_button, second_button] = std::get<std::pair<Button, Button>>(button);
            // 绘制第二个按钮
            nvgTextBounds(vg, x, y - 7.f, getButton(second_button), nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, getButton(second_button), nullptr);
            x -= len + 10.f;
            // 绘制"+"号
            nvgTextBounds(vg, x, y - 7.f, "+", nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, "+", nullptr);
            x -= len + 10.f;
            // 绘制第一个按钮
            nvgTextBounds(vg, x, y - 7.f, getButton(first_button), nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, getButton(first_button), nullptr);
            x -= len + 34.f;
        }
    }
}

// 支持单独设置按钮颜色的drawButtons2函数重载
void drawButtons2Colored(NVGcontext* vg, std::same_as<pair2_colored> auto ...args) {
    const std::array list = {args...};
    nvgBeginPath(vg);
    nvgFontSize(vg, 24.f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

    float x = 1220.f;
    const float y = 675.f;
    float bounds[4]{};

    for (const auto& [button, text, color] : list) {
        // 设置当前按钮的颜色
        nvgFillColor(vg, getColour(color));
        
        nvgFontSize(vg, 20.f);
        nvgTextBounds(vg, x, y, text, nullptr, bounds);
        auto len = bounds[2] - bounds[0];
        nvgText(vg, x, y, text, nullptr);

        x -= len + 10.f;
        
        // 处理单个按钮或组合按钮
        nvgFontSize(vg, 30.f);
        if (std::holds_alternative<Button>(button)) {
            // 单个按钮
            const Button single_button = std::get<Button>(button);
            nvgTextBounds(vg, x, y - 7.f, getButton(single_button), nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, getButton(single_button), nullptr);
            x -= len + 34.f;
        } else {
            // 组合按钮
            const auto& [first_button, second_button] = std::get<std::pair<Button, Button>>(button);
            // 绘制第二个按钮
            nvgTextBounds(vg, x, y - 7.f, getButton(second_button), nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, getButton(second_button), nullptr);
            x -= len + 10.f;
            // 绘制"+"号
            nvgTextBounds(vg, x, y - 7.f, "+", nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, "+", nullptr);
            x -= len + 10.f;
            // 绘制第一个按钮
            nvgTextBounds(vg, x, y - 7.f, getButton(first_button), nullptr, bounds);
            len = bounds[2] - bounds[0];
            nvgText(vg, x, y - 7.f, getButton(first_button), nullptr);
            x -= len + 34.f;
        }
    }
}

using pair = std::pair<Button, const char*>;
void drawButtons(NVGcontext* vg, Colour color = Colour::WHITE, std::same_as<pair> auto ...args) {
    const std::array list = {args...};
    nvgBeginPath(vg);
    nvgFontSize(vg, 24.f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, getColour(color));

    float x = 1220.f;
    const float y = 675.f;
    float bounds[4]{};

    for (const auto& [button, text] : list) {
        nvgFontSize(vg, 20.f);
        nvgTextBounds(vg, x, y, text, nullptr, bounds);
        auto len = bounds[2] - bounds[0];
        nvgText(vg, x, y, text, nullptr);

        x -= len + 10.f;
        nvgFontSize(vg, 30.f);
        nvgTextBounds(vg, x, y - 7.f, getButton(button), nullptr, bounds);
        len = bounds[2] - bounds[0];
        nvgText(vg, x, y - 7.f, getButton(button), nullptr);
        x -= len + 34.f;
    }
}

} // namespace tj::gfx
