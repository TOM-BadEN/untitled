// 应用程序主头文件 (Main application header file)
#include "app.hpp"
// NanoVG工具函数 (NanoVG utility functions)
#include "nvg_util.hpp"
// NanoVG Deko3D渲染器 (NanoVG Deko3D renderer)
#include "nanovg/deko3d/nanovg_dk.h"
// STB图像库 (STB image library)
#include "nanovg/stb_image.h"
// 语言管理器 (Language manager)
#include "lang_manager.hpp"
// 原子操作 (Atomic operations)
#include <atomic>
// 智能指针 (Smart pointers)
#include <memory>
// C字符串操作 (C string operations)
#include <cstring>
// Nintendo Switch应用缓存库 (Nintendo Switch application cache library)
#include <nxtc.h>

// 算法库 (Algorithm library)
#include <algorithm>
// 范围库 (Ranges library)
#include <ranges>

// 调试模式日志宏定义 (Debug mode log macro definition)
#ifndef NDEBUG
    #include <cstdio>
    #define LOG(...) std::printf(__VA_ARGS__)
#else // NDEBUG
    #define LOG(...)
#endif // NDEBUG

namespace tj {

// App类的额外成员变量 (Additional member variables for App class)
// 标记首批应用是否已加载完成 (Flag indicating if initial batch of apps is loaded)
std::atomic<bool> App::initial_batch_loaded{false};
// 已扫描的应用数量 (Number of scanned applications)
std::atomic<size_t> App::scanned_count{0};
// 应用总数量 (Total number of applications)
std::atomic<size_t> App::total_count{0};
// 标记扫描是否正在运行 (Flag indicating if scanning is running)
std::atomic<bool> App::is_scan_running{true};
// 保护应用条目列表的互斥锁 (Mutex to protect application entries list)
std::mutex App::entries_mutex;

namespace {





// 感谢Shchmue的贡献 (Thank you Shchmue ^^)
// 应用程序占用空间条目结构体 (Application occupied size entry structure)
struct ApplicationOccupiedSizeEntry {
    std::uint8_t storageId;        // 存储设备ID (Storage device ID)
    std::uint64_t sizeApplication; // 应用程序本体大小 (Application base size)
    std::uint64_t sizePatch;       // 补丁大小 (Patch size)
    std::uint64_t sizeAddOnContent; // 追加内容大小 (Add-on content size)
};

// 应用程序占用空间结构体 (Application occupied size structure)
struct ApplicationOccupiedSize {
    ApplicationOccupiedSizeEntry entry[4]; // 最多4个存储设备的条目 (Up to 4 storage device entries)
};

// 屏幕尺寸常量 (Screen dimension constants)
constexpr float SCREEN_WIDTH = 1280.f;  // 屏幕宽度 (Screen width)
constexpr float SCREEN_HEIGHT = 720.f;  // 屏幕高度 (Screen height)
constexpr int BATCH_SIZE = 4; // 首批加载的应用数量 (Initial batch size for loading apps)

// 验证JPEG数据完整性的辅助函数
// Helper function to validate JPEG data integrity
bool IsValidJpegData(const std::vector<unsigned char>& data) {
    if (data.size() < 4) return false;
    
    // 检查JPEG文件头和文件尾
    // Check JPEG file header and trailer
    bool has_jpeg_header = (data[0] == 0xFF && data[1] == 0xD8);
    bool has_jpeg_trailer = (data[data.size()-2] == 0xFF && data[data.size()-1] == 0xD9);
    return has_jpeg_header && has_jpeg_trailer;
}


// 异步删除应用程序函数 (Asynchronous application deletion function)
void NsDeleteAppsAsync(std::stop_token stop_token, NsDeleteData&& data) {
    // 遍历所有要删除的应用条目 (Iterate through all application entries to delete)
    for (const auto&p : data.entries) {
        // 先执行删除操作 (Execute deletion operation first)
        const auto result = nsDeleteApplicationCompletely(p);
        if (R_FAILED(result)) {
            data.del_cb(true);  // 删除失败回调 (Deletion failure callback)
        } else {
            data.del_cb(false); // 删除成功回调 (Deletion success callback)
        }
        
        // 删除完成后检查是否需要停止 (Check if stop is requested after deletion completes)
        if (stop_token.stop_requested()) {
            return;
        }
    }
    data.done_cb(); // 所有删除操作完成回调 (All deletion operations completed callback)
}

// 来自游戏卡安装器的脉冲颜色结构体 (Pulse color structure from gamecard installer)
struct PulseColour {
    NVGcolor col{0, 255, 187, 255}; // 颜色值 (Color value)
    u8 delay;                       // 延迟时间 (Delay time)
    bool increase_blue;             // 是否增加蓝色分量 (Whether to increase blue component)
};

PulseColour pulse; //脉冲颜色对象(Pulse color object)

void update_pulse_colour() { //更新脉冲颜色函数(Update pulse color function)
    if (pulse.col.g == 255) { //当绿色分量达到最大值时(When green component reaches maximum)
        pulse.increase_blue = true; //开始增加蓝色分量(Start increasing blue component)
    } else if (pulse.col.b == 255 && pulse.delay == 10) { //当蓝色分量达到最大值且延迟达到10时(When blue component reaches maximum and delay reaches 10)
        pulse.increase_blue = false; //停止增加蓝色分量(Stop increasing blue component)
        pulse.delay = 0; //重置延迟计数器(Reset delay counter)
    }

    if (pulse.col.b == 255 && pulse.increase_blue == true) { //当蓝色分量为最大值且正在增加蓝色时(When blue component is at maximum and increasing blue)
        pulse.delay++; //增加延迟计数器(Increment delay counter)
    } else {
        pulse.col.b = pulse.increase_blue ? pulse.col.b + 2 : pulse.col.b - 2; //根据方向调整蓝色分量(Adjust blue component based on direction)
        pulse.col.g = pulse.increase_blue ? pulse.col.g - 2 : pulse.col.g + 2; //根据方向调整绿色分量(Adjust green component based on direction)
    }
}

} // namespace

// 格式化存储大小的辅助函数 (Helper function to format storage size)
// 将字节数转换为可读的存储大小字符串 (Convert bytes to readable storage size string)
std::string FormatStorageSize(std::size_t size_bytes) {
    if (size_bytes == 0) { // 如果大小为0则返回空字符串 (Return empty string if size is 0)
        return "";
    }
    
    // 计算GB值 (Calculate GB value)
    float size_in_gb = static_cast<float>(size_bytes) / 0x40000000; // 1GB = 1024^3 bytes
    
    if (size_in_gb >= 1.0f) { // 如果大于等于1GB则显示GB单位 (Display GB unit if >= 1GB)
        // 显示GB单位，保留一位小数 (Display GB unit with one decimal place)
        char buffer[32]; // 缓冲区用于格式化字符串 (Buffer for formatted string)
        snprintf(buffer, sizeof(buffer), "%.1f GB", size_in_gb); // 格式化为GB字符串 (Format as GB string)
        return std::string(buffer); // 返回格式化后的字符串 (Return formatted string)
    } else {
        // 显示MB单位，保留一位小数 (Display MB unit with one decimal place)
        float size_in_mb = static_cast<float>(size_bytes) / 0x100000; // 1MB = 1024^2 bytes
        char buffer[32]; // 缓冲区用于格式化字符串 (Buffer for formatted string)
        snprintf(buffer, sizeof(buffer), "%.1f MB", size_in_mb); // 格式化为MB字符串 (Format as MB string)
        return std::string(buffer); // 返回格式化后的字符串 (Return formatted string)
    }
}

// ResourceLoadManager 实现 (ResourceLoadManager implementation)
// 资源加载管理器的具体实现 (Concrete implementation of resource load manager)
void ResourceLoadManager::submitLoadTask(const ResourceLoadTask& task) { // 提交加载任务 (Submit loading task)
    std::scoped_lock lock{task_mutex}; // 获取互斥锁保护任务队列 (Acquire mutex lock to protect task queue)
    pending_tasks.push(task); // 将任务添加到待处理队列 (Add task to pending queue)
}

void ResourceLoadManager::processFrameLoads() { // 处理每帧的加载任务 (Process loading tasks per frame)
    int icon_loads_this_frame = 0; // 当前帧已加载的图标数量 (Number of icons loaded in current frame)
    
    std::scoped_lock lock{task_mutex}; // 获取互斥锁保护任务队列 (Acquire mutex lock to protect task queue)
    
    // 处理任务，只对图标任务限制每帧最多2个 (Process tasks, only limit icon tasks to max 2 per frame)
    // 这样可以避免图标加载造成的帧率下降 (This prevents frame rate drops caused by icon loading)
    while (!pending_tasks.empty()) { // 当队列不为空时继续处理 (Continue processing while queue is not empty)
        auto task = pending_tasks.top(); // 获取优先级最高的任务 (Get highest priority task)
        
        // 如果是图标任务且已达到每帧图标加载限制，则停止处理图标任务 (If it's an icon task and we've reached the per-frame icon loading limit, stop processing icon tasks)
        // 但仍然尝试处理其他类型的任务以保持响应性 (But still try to process other types of tasks to maintain responsiveness)
        if (task.task_type == ResourceTaskType::ICON && icon_loads_this_frame >= MAX_ICON_LOADS_PER_FRAME) {
            // 检查是否还有非图标任务可以处理 (Check if there are non-icon tasks that can be processed)
            // 这确保了非图标任务不会被图标任务阻塞 (This ensures non-icon tasks are not blocked by icon tasks)
            std::vector<ResourceLoadTask> temp_tasks; // 临时存储任务的容器 (Temporary container for storing tasks)
            bool found_non_icon = false; // 是否找到非图标任务的标志 (Flag indicating if non-icon task is found)
            
            // 临时移除任务来查找非图标任务 (Temporarily remove tasks to find non-icon tasks)
            // 这是一个线性搜索过程，但任务队列通常不会很大 (This is a linear search process, but task queue is usually not large)
            while (!pending_tasks.empty()) {
                auto temp_task = pending_tasks.top(); // 获取队列顶部任务 (Get top task from queue)
                pending_tasks.pop(); // 从队列中移除任务 (Remove task from queue)
                temp_tasks.push_back(temp_task); // 添加到临时容器 (Add to temporary container)
                
                if (temp_task.task_type != ResourceTaskType::ICON) { // 如果不是图标任务 (If it's not an icon task)
                    found_non_icon = true; // 标记找到非图标任务 (Mark that non-icon task is found)
                    task = temp_task; // 使用找到的非图标任务 (Use the found non-icon task)
                    temp_tasks.pop_back(); // 移除找到的非图标任务 (Remove the found non-icon task)
                    break; // 跳出搜索循环 (Break out of search loop)
                }
            }
            
            // 将临时移除的任务放回队列 (Put temporarily removed tasks back to queue)
            // 保持任务队列的完整性 (Maintain integrity of task queue)
            for (const auto& temp_task : temp_tasks) {
                pending_tasks.push(temp_task); // 重新添加到队列 (Re-add to queue)
            }
            
            if (!found_non_icon) { // 如果没有找到非图标任务 (If no non-icon task is found)
                break; // 没有可处理的任务，退出循环 (No processable tasks, exit loop)
            }
        } else {
            pending_tasks.pop(); // 从队列中移除当前任务 (Remove current task from queue)
        }
        
        // 执行加载任务 (Execute loading task)
        // 调用任务的回调函数来执行实际的加载操作 (Call task's callback function to perform actual loading operation)
        if (task.load_callback) { // 如果回调函数存在 (If callback function exists)
            task.load_callback(); // 执行回调函数 (Execute callback function)
        }
        
        // 只对图标任务计数 (Only count icon tasks)
        // 这样可以准确控制每帧的图标加载数量 (This allows accurate control of icon loading count per frame)
        if (task.task_type == ResourceTaskType::ICON) {
            icon_loads_this_frame++; // 增加图标加载计数 (Increment icon loading count)
        }
    }
}

bool ResourceLoadManager::hasPendingTasks() const { // 检查是否有待处理任务 (Check if there are pending tasks)
    std::scoped_lock lock{task_mutex}; // 获取互斥锁保护任务队列 (Acquire mutex lock to protect task queue)
    return !pending_tasks.empty(); // 返回队列是否非空 (Return whether queue is not empty)
}

size_t ResourceLoadManager::getPendingTaskCount() const { // 获取待处理任务数量 (Get pending task count)
    std::scoped_lock lock{task_mutex}; // 获取互斥锁保护任务队列 (Acquire mutex lock to protect task queue)
    return pending_tasks.size(); // 返回队列大小 (Return queue size)
}

void App::Loop() { // 应用程序主循环方法 (Application main loop method)
    // 60FPS限制：每帧16.666667毫秒 (Frame rate limit: 16.666667ms per frame for 60FPS)
    // 这确保了游戏在Switch上的流畅运行 (This ensures smooth gameplay on Switch)
    constexpr auto target_frame_time = std::chrono::microseconds(16667); // 1/60秒 (1/60 second)
    auto last_frame_time = std::chrono::steady_clock::now(); // 记录上一帧的时间戳 (Record timestamp of last frame)
    
    // 主循环：持续运行直到退出或applet停止 (Main loop: continue running until quit or applet stops)
    // appletMainLoop()检查Switch系统是否允许应用继续运行 (appletMainLoop() checks if Switch system allows app to continue)
    while (!this->quit && appletMainLoop()) {
        auto frame_start = std::chrono::steady_clock::now(); // 记录当前帧开始时间 (Record current frame start time)
        
        // 执行每帧的核心操作 (Execute core operations per frame)
        this->Poll(); // 处理输入事件 (Process input events)
        this->Update(); // 更新游戏逻辑 (Update game logic)
        this->Draw(); // 渲染画面 (Render graphics)
        
        // 计算帧时间并限制到60FPS (Calculate frame time and limit to 60FPS)
        // 这是帧率控制的关键部分 (This is the key part of frame rate control)
        auto frame_end = std::chrono::steady_clock::now(); // 记录当前帧结束时间 (Record current frame end time)
        auto frame_duration = frame_end - frame_start; // 计算帧处理耗时 (Calculate frame processing time)
        
        // 如果帧处理时间少于目标时间，则休眠剩余时间 (If frame processing time is less than target, sleep for remaining time)
        // 这确保了稳定的60FPS帧率 (This ensures stable 60FPS frame rate)
        if (frame_duration < target_frame_time) {
            auto sleep_time = target_frame_time - frame_duration; // 计算需要休眠的时间 (Calculate sleep time needed)
            auto sleep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_time).count(); // 转换为纳秒 (Convert to nanoseconds)
            svcSleepThread(sleep_ns); // 使用Switch系统调用进行精确休眠 (Use Switch system call for precise sleep)
        }
        
        // 计算实际FPS (Calculate actual FPS) - 在延时后计算以获得准确的帧时间
        // 这用于性能监控和调试 (This is used for performance monitoring and debugging)
        auto current_time = std::chrono::steady_clock::now(); // 获取当前时间戳 (Get current timestamp)
        auto total_frame_time = current_time - last_frame_time; // 计算完整帧时间（包括休眠） (Calculate complete frame time including sleep)
        auto frame_time_us = std::chrono::duration_cast<std::chrono::microseconds>(total_frame_time).count(); // 转换为微秒 (Convert to microseconds)
        if (frame_time_us > 0) { // 避免除零错误 (Avoid division by zero)
            this->FPS = 1000000.0f / static_cast<float>(frame_time_us); // 转换为FPS (Convert to FPS)
        }
        
        last_frame_time = current_time; // 更新上一帧时间戳 (Update last frame timestamp)
    }
}

void Controller::UpdateButtonHeld(bool& down, bool held) { // 更新按钮长按状态的方法 (Method to update button held state)
    // 处理按钮长按的加速重复逻辑 (Handle accelerated repeat logic for button holding)
    if (down) { // 如果按钮刚被按下 (If button was just pressed)
        this->step = 50; // 重置步长为初始值 (Reset step to initial value)
        this->counter = 0; // 重置计数器 (Reset counter)
    } else if (held) { // 如果按钮持续被按住 (If button is continuously held)
        this->counter += this->step; // 增加计数器 (Increment counter)

        // 当计数器达到阈值时触发重复按下事件 (Trigger repeat press event when counter reaches threshold)
        // 这实现了长按时的加速重复功能 (This implements accelerated repeat functionality during long press)
        if (this->counter >= this->MAX) {
            down = true; // 设置按下状态为真 (Set pressed state to true)
            this->counter = 0; // 重置计数器准备下次重复 (Reset counter for next repeat)
            this->step = std::min(this->step + 50, this->MAX_STEP); // 增加步长实现加速，但不超过最大值 (Increase step for acceleration, but not exceeding maximum)
        }
    }
}

void App::Poll() { // 输入事件轮询方法 (Input event polling method)
    // 输入事件处理时间限制：最大3毫秒，防止复杂输入处理影响帧率 (Input processing time limit: max 3ms to prevent complex input handling from affecting frame rate)
    // 这确保了即使在复杂输入处理时也能维持60FPS (This ensures 60FPS is maintained even during complex input processing)
    constexpr auto max_input_time = std::chrono::microseconds(3000); // 3毫秒 (3 milliseconds)
    auto input_start = std::chrono::steady_clock::now(); // 记录输入处理开始时间 (Record input processing start time)
    
    padUpdate(&this->pad); // 更新手柄状态，从Switch系统获取最新输入数据 (Update controller state, get latest input data from Switch system)

    const auto down = padGetButtonsDown(&this->pad); // 获取本帧新按下的按钮 (Get buttons pressed this frame)
    const auto held = padGetButtons(&this->pad); // 获取当前持续按住的按钮 (Get buttons currently held)

    // 检查是否超时，如果超时则跳过复杂的按键处理 (Check for timeout, skip complex key processing if timed out)
    // 这是第一个超时检查点，确保基础输入获取不会超时 (This is the first timeout checkpoint, ensuring basic input acquisition doesn't timeout)
    auto current_time = std::chrono::steady_clock::now(); // 获取当前时间 (Get current time)
    if (current_time - input_start >= max_input_time) {
        return; // 超时保护，直接返回避免影响帧率 (Timeout protection, return directly to avoid affecting frame rate)
    }

    // 基础按钮状态映射 (Basic button state mapping)
    // 使用位运算检测各个按钮的按下状态 (Use bitwise operations to detect button press states)
    this->controller.A = down & HidNpadButton_A; // A按钮状态 (A button state)
    this->controller.B = down & HidNpadButton_B; // B按钮状态 (B button state)
    this->controller.X = down & HidNpadButton_X; // X按钮状态 (X button state)
    this->controller.Y = down & HidNpadButton_Y; // Y按钮状态 (Y button state)
    this->controller.L = down & HidNpadButton_L; // L肩键状态 (L shoulder button state)
    this->controller.R = down & HidNpadButton_R; // R肩键状态 (R shoulder button state)
    this->controller.L2 = down & HidNpadButton_ZL; // ZL扳机键状态 (ZL trigger button state)
    this->controller.R2 = down & HidNpadButton_ZR; // ZR扳机键状态 (ZR trigger button state)
    this->controller.START = down & HidNpadButton_Plus; // +按钮（开始键）状态 (+ button (start key) state)
    this->controller.SELECT = down & HidNpadButton_Minus; // -按钮（选择键）状态 (- button (select key) state)
    this->controller.RIGHT = down & HidNpadButton_AnyRight; // 右方向键状态 (Right directional key state)
    
    // RIGHT+A组合键检测 (RIGHT+A combination key detection)
    // 简单检测：只要两个键都按下就触发，避免重复触发 (Simple detection: trigger when both keys are pressed, avoid repeated triggering)
    bool right_held = held & HidNpadButton_Right; // 右方向键是否被持续按住 (Whether Right key is being held)
    bool a_held = held & HidNpadButton_A; // A键是否被持续按住 (Whether A key is being held)
    
    // 保存上一帧的组合键状态用于避免重复触发 (Save previous frame combo state to avoid repeated triggering)
    static bool prev_combo_triggered = false;
    
    // 组合键触发条件：两个键都按下且上一帧未触发 (Combination key trigger condition: both keys pressed and not triggered in previous frame)
    if (right_held && a_held && !prev_combo_triggered) {
        this->controller.RIGHT_AND_A = true;
        prev_combo_triggered = true; // 标记已触发，避免重复触发 (Mark as triggered to avoid repeated triggering)
    } else if (!right_held || !a_held) {
        // 任一键释放时重置触发状态 (Reset trigger state when any key is released)
        this->controller.RIGHT_AND_A = false;
        prev_combo_triggered = false;
    } else {
        this->controller.RIGHT_AND_A = false;
    }
    
    // 再次检查时间，避免方向键处理超时 (Check time again to avoid directional key processing timeout)
    // 这是第二个超时检查点，确保方向键处理有足够时间 (This is the second timeout checkpoint, ensuring directional key processing has enough time)
    current_time = std::chrono::steady_clock::now(); // 更新当前时间 (Update current time)
    if (current_time - input_start >= max_input_time) {
        return; // 超时保护 (Timeout protection)
    }
    
    // 方向键状态处理 (Directional key state processing)
    // keep directional keys pressed. 保持方向键按下状态 (Keep directional keys pressed)
    this->controller.DOWN = (down & HidNpadButton_AnyDown); // 下方向键状态 (Down directional key state)
    this->controller.UP = (down & HidNpadButton_AnyUp); // 上方向键状态 (Up directional key state)
    this->controller.LEFT = (down & HidNpadButton_AnyLeft); // 左方向键状态 (Left directional key state)
    

    // 更新方向键的长按状态 (Update directional key held states)
    // 这些调用处理方向键的加速重复功能 (These calls handle accelerated repeat functionality for directional keys)
    this->controller.UpdateButtonHeld(this->controller.DOWN, held & HidNpadButton_AnyDown); // 更新下键长按状态 (Update down key held state)
    this->controller.UpdateButtonHeld(this->controller.UP, held & HidNpadButton_AnyUp); // 更新上键长按状态 (Update up key held state)

#ifndef NDEBUG // 调试模式编译条件 (Debug mode compilation condition)
    // 调试输出也需要时间检查 (Debug output also needs time checking)
    // 确保调试输出不会影响帧率性能 (Ensure debug output doesn't affect frame rate performance)
    current_time = std::chrono::steady_clock::now(); // 获取当前时间用于调试输出时间检查 (Get current time for debug output time checking)
    if (current_time - input_start < max_input_time) { // 只有在时间限制内才执行调试输出 (Only execute debug output within time limit)
        // Lambda函数：用于简化按键状态的调试输出 (Lambda function: simplify debug output for key states)
        // 这个函数只在按键被按下时才输出日志 (This function only outputs log when key is pressed)
        auto display = [](const char* str, bool key) {
            if (key) { // 如果按键被按下 (If key is pressed)
                LOG("Key %s is Pressed\n", str); // 输出按键按下的调试信息 (Output debug info for key press)
            }
        };

        // 调试输出各个按键的状态 (Debug output for each key state)
        // 这些调用帮助开发者了解当前的输入状态 (These calls help developers understand current input state)
        display("A", this->controller.A); // 输出A键状态 (Output A key state)
        display("B", this->controller.B); // 输出B键状态 (Output B key state)
        display("X", this->controller.X); // 输出X键状态 (Output X key state)
        display("Y", this->controller.Y); // 输出Y键状态 (Output Y key state)
        display("L", this->controller.L); // 输出L肩键状态 (Output L shoulder key state)
        display("R", this->controller.R); // 输出R肩键状态 (Output R shoulder key state)
        display("L2", this->controller.L2); // 输出ZL扳机键状态 (Output ZL trigger key state)
        display("R2", this->controller.R2); // 输出ZR扳机键状态 (Output ZR trigger key state)
    }
#endif // 结束调试模式条件编译 (End debug mode conditional compilation)
} // App::Poll()方法结束 (End of App::Poll() method)

// App::Update() - 应用程序主更新方法 (Main application update method)
// 每帧调用一次，处理应用程序的核心逻辑更新 (Called once per frame to handle core application logic updates)
void App::Update() {
    // 每帧处理资源加载（如果启用） (Process resource loading per frame if enabled)
    // 这个机制用于分散资源加载的CPU负担，避免单帧卡顿 (This mechanism distributes CPU load of resource loading to avoid single frame stuttering)
    if (enable_frame_load_limit) { // 检查是否启用了帧限制资源加载 (Check if frame-limited resource loading is enabled)
        resource_manager.processFrameLoads(); // 处理当前帧允许的资源加载任务 (Process resource loading tasks allowed for current frame)
    }
    
    // 根据当前菜单模式执行相应的更新逻辑 (Execute corresponding update logic based on current menu mode)
    // 使用状态机模式管理不同的应用程序状态 (Use state machine pattern to manage different application states)
    switch (this->menu_mode) {
        case MenuMode::LOAD: // 加载模式：应用程序启动时的初始化状态 (Load mode: initialization state during app startup)
            this->UpdateLoad(); // 处理应用程序加载逻辑 (Handle application loading logic)
            break;
        case MenuMode::LIST: // 列表模式：显示应用程序列表的主界面 (List mode: main interface showing application list)
            this->UpdateList(); // 处理应用程序列表的交互和显示 (Handle application list interaction and display)
            break;
        case MenuMode::CONFIRM: // 确认模式：用户确认操作的对话框状态 (Confirm mode: dialog state for user operation confirmation)
            this->UpdateConfirm(); // 处理确认对话框的逻辑 (Handle confirmation dialog logic)
            break;
    }
} // App::Update()方法结束 (End of App::Update() method)

// App::Draw() - 应用程序主渲染方法 (Main application rendering method)
// 每帧调用一次，负责所有的GPU渲染操作 (Called once per frame, responsible for all GPU rendering operations)
void App::Draw() {
    // GPU命令优化：准备下一个命令缓冲区 (GPU command optimization: Prepare next command buffer)
    // GPU command optimization: Prepare next command buffer
    // 双缓冲机制，避免GPU等待CPU准备命令 (Double buffering mechanism to avoid GPU waiting for CPU command preparation)
    this->prepareNextCommandBuffer();
    
    // 从交换链获取可用的图像槽位 (Acquire available image slot from swapchain)
    // 这是Vulkan/deko3d渲染管线的标准流程 (This is standard Vulkan/deko3d rendering pipeline procedure)
    const auto slot = this->queue.acquireImage(this->swapchain);
    
    // 提交静态命令（帧缓冲区绑定和基础渲染状态） (Submit static commands - framebuffer binding and basic render state)
    // Submit static commands (framebuffer binding and basic render state)
    // 静态命令在应用启动时预先录制，提高渲染效率 (Static commands are pre-recorded at app startup for rendering efficiency)
    this->queue.submitCommands(this->framebuffer_cmdlists[slot]); // 提交帧缓冲区绑定命令 (Submit framebuffer binding commands)
    this->queue.submitCommands(this->render_cmdlist); // 提交基础渲染状态命令 (Submit basic render state commands)
    
    // 开始记录动态命令到当前命令缓冲区 (Start recording dynamic commands to current command buffer)
    // Start recording dynamic commands to current command buffer
    // 动态命令每帧重新录制，包含实际的绘制调用 (Dynamic commands are re-recorded each frame, containing actual draw calls)
    auto& current_cmdbuf = this->dynamic_cmdbufs[this->current_cmdbuf_index]; // 获取当前帧的命令缓冲区 (Get command buffer for current frame)
    current_cmdbuf.clear(); // 清空上一帧的命令 (Clear commands from previous frame)
    
    // NanoVG渲染命令 (NanoVG rendering commands)
    // NanoVG rendering commands
    // 开始NanoVG帧渲染，设置屏幕尺寸和像素比 (Begin NanoVG frame rendering with screen size and pixel ratio)
    nvgBeginFrame(this->vg, SCREEN_WIDTH, SCREEN_HEIGHT, 1.f);
    
    // 绘制背景元素（标题栏、分割线等） (Draw background elements - title bar, dividers, etc.)
    this->DrawBackground();
    
    // 根据当前菜单模式绘制相应的界面内容 (Draw corresponding interface content based on current menu mode)
    // 使用状态机模式管理不同界面的渲染逻辑 (Use state machine pattern to manage rendering logic for different interfaces)
    switch (this->menu_mode) {
        case MenuMode::LOAD: // 加载界面：显示启动画面和初始化进度 (Load interface: show startup screen and initialization progress)
            this->DrawLoad(); // 绘制加载界面 (Draw loading interface)
            break;
        case MenuMode::LIST: // 列表界面：显示应用程序列表和选择器 (List interface: show application list and selector)
            this->DrawList(); // 绘制应用程序列表 (Draw application list)
            break;
        case MenuMode::CONFIRM: // 确认界面：显示操作确认对话框 (Confirm interface: show operation confirmation dialog)
            this->DrawConfirm(); // 绘制确认对话框 (Draw confirmation dialog)
            break;
    }

    // 结束NanoVG帧渲染，提交所有绘制命令到GPU (End NanoVG frame rendering, submit all draw commands to GPU)
    nvgEndFrame(this->vg);
    
    // 完成动态命令列表记录 (Finish dynamic command list recording)
    // Finish dynamic command list recording
    // 将录制的命令转换为可执行的命令列表 (Convert recorded commands to executable command list)
    this->dynamic_cmdlists[this->current_cmdbuf_index] = current_cmdbuf.finishList();
    
    // 异步提交动态命令（不阻塞CPU） (Asynchronously submit dynamic commands - non-blocking CPU)
    // Asynchronously submit dynamic commands (non-blocking CPU)
    // 允许CPU继续处理下一帧，提高整体性能 (Allow CPU to continue processing next frame for better overall performance)
    this->submitCurrentCommandBuffer();
    
    // 呈现图像 (Present image)
    // Present image
    // 将渲染结果显示到屏幕上 (Display rendering result to screen)
    this->queue.presentImage(this->swapchain, slot);
} // App::Draw()方法结束 (End of App::Draw() method)

// App::DrawBackground() - 绘制应用程序背景界面 (Draw application background interface)
// 包含背景色、分割线、标题和版本信息 (Includes background color, dividers, title and version info)
void App::DrawBackground() {
    
    // 绘制全屏黑色背景 (Draw full-screen black background)
    // 作为所有UI元素的基础背景层 (Serves as base background layer for all UI elements)
    gfx::drawRect(this->vg, 0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT, gfx::Colour::BLACK);
    
    // 绘制顶部分割线 (Draw top divider line)
    // 分离标题区域和主内容区域 (Separate title area from main content area)
    gfx::drawRect(vg, 30.f, 86.0f, 1220.f, 1.f, gfx::Colour::WHITE);
    
    // 绘制底部分割线 (Draw bottom divider line)
    // 分离主内容区域和底部状态栏 (Separate main content area from bottom status bar)
    gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, gfx::Colour::WHITE);
    
// 版本字符串宏定义 (Version string macro definitions)
// uses the APP_VERSION define in makefile for string version.
// source: https://stackoverflow.com/a/2411008
// 将宏值转换为字符串的标准C预处理器技巧 (Standard C preprocessor trick to convert macro values to strings)
#define STRINGIZE(x) #x // 第一层宏：将参数转换为字符串字面量 (First-level macro: convert parameter to string literal)
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x) // 第二层宏：先展开宏值再转换为字符串 (Second-level macro: expand macro value then convert to string)
    
    // 根据扫描状态决定是否显示进度 (Decide whether to show progress based on scanning status)
    // 动态显示应用程序扫描的实时状态 (Dynamically show real-time status of application scanning)
    if (!finished_scanning) { // 如果扫描尚未完成 (If scanning is not yet finished)
        // 扫描未完成，显示"扫描中 xx/xx" (Scanning incomplete, show "Scanning xx/xx")
        // 使用原子变量确保线程安全的计数器读取 (Use atomic variables for thread-safe counter reading)
        gfx::drawTextArgs(this->vg, 70.f, 40.f, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE, software_title_loading.c_str(), software_title.c_str(), scanned_count.load(), total_count.load());
    } else { // 扫描已完成 (Scanning completed)
        // 扫描完成，只显示标题 (Scanning complete, show only title)
        // 清洁的界面，不显示进度信息 (Clean interface without progress information)
        gfx::drawText(this->vg, 70.f, 40.f, 28.f, software_title.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
    }
    
    // 在右上角显示应用程序版本号 (Display application version number in top-right corner)
    // 使用银色字体，较小尺寸，不干扰主要内容 (Use silver font, smaller size, doesn't interfere with main content)
    gfx::drawText(this->vg, 1224.f, 45.f, 22.f, STRINGIZE_VALUE_OF(UNTITLED_VERSION_STRING), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::SILVER);
    
// 清理宏定义，避免污染全局命名空间 (Clean up macro definitions to avoid polluting global namespace)
#undef STRINGIZE
#undef STRINGIZE_VALUE_OF

} // App::DrawBackground()方法结束 (End of App::DrawBackground() method)

// App::DrawLoad() - 绘制加载界面 (Draw loading interface)
// 显示应用程序扫描进度和加载状态信息 (Display application scanning progress and loading status information)
void App::DrawLoad() {
    // 显示扫描进度 (Display scanning progress)
    // 使用作用域锁保护共享数据，确保线程安全 (Use scoped lock to protect shared data for thread safety)
    // 防止在读取loading_text时发生数据竞争 (Prevent data races when reading loading_text)
    std::scoped_lock lock{entries_mutex};
    
    // 在屏幕中央显示加载文本 (Display loading text in screen center)
    // 使用黄色突出显示，36px字体大小，居中对齐 (Use yellow highlight, 36px font size, center alignment)
    // 位置稍微上移40像素，为底部按钮留出空间 (Position slightly moved up 40 pixels to leave space for bottom buttons)
    gfx::drawTextArgs(this->vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f - 40.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, gfx::Colour::YELLOW, loading_text.c_str());
    
    // 在底部显示返回按钮提示 (Display back button hint at bottom)
    // 白色文字，显示B键对应的返回操作 (White text, showing B key corresponding to back operation)
    // 为用户提供退出加载界面的选项 (Provide user with option to exit loading interface)
    gfx::drawButtons(this->vg, gfx::Colour::WHITE, gfx::pair{gfx::Button::B, button_back.c_str()});
} // App::DrawLoad()方法结束 (End of App::DrawLoad() method)


// App::DrawList() - 绘制应用程序列表界面 (Draw application list interface)
// 显示可删除的应用程序列表，包含图标、标题、大小信息和选择状态 (Display deletable application list with icons, titles, size info and selection status)
void App::DrawList() {
    // 如果没有应用，显示加载提示 (If no apps, show loading hint)
    // If no apps, show loading hint
    // 处理应用列表为空的边界情况 (Handle edge case when application list is empty)
    if (this->entries.empty()) {
        // 在屏幕中央显示加载提示文本 (Display loading hint text in screen center)
        gfx::drawTextBoxCentered(this->vg, 90.f, 130.f, 715.f, 516.f, 35.f, 1.5f, no_app_found.c_str(), nullptr, gfx::Colour::SILVER);
        return; // 提前返回，不执行后续的列表绘制逻辑 (Early return, skip subsequent list drawing logic)
    }
    
    // UI布局常量定义 (UI layout constant definitions)
    // 这些常量定义了列表项和侧边栏的精确布局参数 (These constants define precise layout parameters for list items and sidebar)
    
    // 定义列表项的高度 (120像素) (Define list item height - 120 pixels)
    // 每个应用条目的垂直空间 (Vertical space for each application entry)
    constexpr auto box_height = 120.f;
    
    // 定义列表项的宽度 (715像素) (Define list item width - 715 pixels)
    // 左侧应用列表区域的水平空间 (Horizontal space for left application list area)
    constexpr auto box_width = 715.f;
    
    // 定义图标与边框的间距 (12像素) (Define spacing between icon and border - 12 pixels)
    // 应用图标距离列表项边框的内边距 (Inner padding of app icon from list item border)
    constexpr auto icon_spacing = 12.f;
    
    // 定义标题距离左侧的间距 (116像素) (Define title spacing from left - 116 pixels)
    // 应用标题文本的左侧起始位置 (Left starting position of application title text)
    constexpr auto title_spacing_left = 116.f;
    
    // 定义标题距离顶部的间距 (30像素) (Define title spacing from top - 30 pixels)
    // 应用标题文本的垂直位置 (Vertical position of application title text)
    constexpr auto title_spacing_top = 30.f;
    
    // 定义文本距离左侧的间距 (与标题左侧间距相同) (Define text spacing from left - same as title left spacing)
    // 应用详细信息文本的左侧对齐位置 (Left alignment position for application detail text)
    constexpr auto text_spacing_left = title_spacing_left;
    
    // 定义文本距离顶部的间距 (67像素) (Define text spacing from top - 67 pixels)
    // 应用详细信息文本的垂直位置 (Vertical position of application detail text)
    constexpr auto text_spacing_top = 67.f;
    
    // 定义右侧信息框的X坐标 (870像素) (Define right info box X coordinate - 870 pixels)
    // 右侧信息面板的水平起始位置 (Horizontal starting position of right info panel)
    constexpr auto sidebox_x = 870.f;
    
    // 定义右侧信息框的Y坐标 (87像素) (Define right info box Y coordinate - 87 pixels)
    // 右侧信息面板的垂直起始位置 (Vertical starting position of right info panel)
    constexpr auto sidebox_y = 87.f;
    
    // 定义右侧信息框的宽度 (380像素) (Define right info box width - 380 pixels)
    // 右侧信息面板的水平尺寸 (Horizontal dimension of right info panel)
    constexpr auto sidebox_w = 380.f;
    
    // 定义右侧信息框的高度 (558像素) (Define right info box height - 558 pixels)
    // 右侧信息面板的垂直尺寸 (Vertical dimension of right info panel)
    constexpr auto sidebox_h = 558.f;

    // 计算已选中应用在各存储设备上的总容量 (Calculate total capacity of selected apps on each storage device)
    // Calculate total capacity of selected apps on each storage device
    // 统计用户选择删除的应用程序占用的存储空间 (Count storage space occupied by applications selected for deletion)
    std::size_t selected_nand_total = 0; // NAND内置存储的总占用量 (Total occupation of NAND internal storage)
    std::size_t selected_sd_total = 0;   // SD卡存储的总占用量 (Total occupation of SD card storage)
    
    // 遍历所有应用条目，累计已选中应用的存储占用 (Iterate through all app entries, accumulate storage occupation of selected apps)
    for (const auto& entry : this->entries) {
        if (entry.selected) { // 如果当前应用被选中删除 (If current app is selected for deletion)
            selected_nand_total += entry.size_nand; // 累加NAND存储占用 (Accumulate NAND storage occupation)
            selected_sd_total += entry.size_sd;     // 累加SD卡存储占用 (Accumulate SD card storage occupation)
        }
    }


    /**
     * @brief 绘制存储条的lambda函数
     * 
     * @param str 存储设备名称（如"系统内存"或"microSD卡"）
     * @param x 绘制起始X坐标
     * @param y 绘制起始Y坐标
     * @param storage_size 总存储大小
     * @param storage_free 可用存储大小
     * @param storage_used 已使用存储大小
     * @param app_size 当前应用占用大小
     */
    // Lambda函数：绘制存储容量可视化界面 (Lambda function: Draw storage capacity visualization interface)
    const auto draw_size = [&](const char* str, float x, float y, std::size_t storage_size, std::size_t storage_free, std::size_t storage_used, std::size_t app_size) {
        // 绘制存储设备名称文本 (Draw storage device name text)
        gfx::drawText(this->vg, x, y-5.f, 22.f, str, nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
        // 绘制存储条白色外框 (Draw storage bar white border)
        gfx::drawRect(this->vg, x - 5.f, y + 28.f, 326.f, 16.f, gfx::Colour::WHITE);
        // 绘制存储条黑色背景 (Draw storage bar black background)
        gfx::drawRect(this->vg, x - 4.f, y + 29.f, 326.f - 2.f, 16.f - 2.f, gfx::Colour::LIGHT_BLACK);
        // 计算已使用存储条宽度 (Calculate used storage bar width)
        const auto bar_width = (static_cast<float>(storage_used) / static_cast<float>(storage_size)) * (326.f - 4.f);
        // 计算当前应用占用存储条宽度 (Calculate current app storage bar width)
        const auto used_bar_width = (static_cast<float>(app_size) / static_cast<float>(storage_size)) * (326.f - 4.f);
        // 绘制已使用存储条(白色) (Draw used storage bar - white)
        gfx::drawRect(this->vg, x - 3.f, y + 30.f, bar_width, 16.f - 4.f, gfx::Colour::WHITE);
        // 绘制当前应用占用存储条(青色) (Draw current app storage bar - cyan)
        gfx::drawRect(this->vg, x - 3.f + bar_width - used_bar_width, y + 30.f, used_bar_width, 16.f - 4.f, gfx::Colour::CYAN);
        // 绘制"可用空间"文本 (Draw "available space" text)
        gfx::drawText(this->vg, x, y + 60.f, 20.f, space_available.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
        // 绘制可用空间大小(GB) (Draw available space size in GB)
        gfx::drawTextArgs(this->vg, x + 315.f, y + 60.f, 24.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::WHITE, "%.1f GB", static_cast<float>(storage_free) / static_cast<float>(0x40000000));
        
    };
    
    // 绘制右侧信息框背景 (Draw right info box background)
    // 注：此处原本尝试使用线性渐变，但未实现 (Note: Originally tried to use linear gradient, but not implemented)
    gfx::drawRect(this->vg, sidebox_x, sidebox_y, sidebox_w, sidebox_h, gfx::Colour::LIGHT_BLACK);
    
    // 绘制系统内存存储条 (Draw system memory storage bar)
    draw_size(system_memory.c_str(), sidebox_x + 30.f, sidebox_y + 56.f, this->nand_storage_size_total, this->nand_storage_size_free, this->nand_storage_size_used, selected_nand_total);
    
    // 绘制microSD卡存储条 (Draw microSD card storage bar)
    draw_size(micro_sd_card.c_str(), sidebox_x + 30.f, sidebox_y + 235.f, this->sdcard_storage_size_total, this->sdcard_storage_size_free, this->sdcard_storage_size_used, selected_sd_total);

    // 显示NAND选中应用的总容量 (Display total capacity of selected NAND apps)
    if (selected_nand_total > 0){
            gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 56.f + 85.f, 20.f, total_selected.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 56.f + 85.f, 24.f, (plus_sign + FormatStorageSize(selected_nand_total)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
        }
    
    // 显示SD卡选中应用的总容量 (Display total capacity of selected SD apps)
    if (selected_sd_total > 0){
            gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 235.f + 85.f, 20.f, total_selected.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 235.f + 85.f, 24.f, (plus_sign + FormatStorageSize(selected_sd_total)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
    }
    
    // 保存当前绘图状态并设置裁剪区域 (Save current drawing state and set clipping area)
    nvgSave(this->vg);
    nvgScissor(this->vg, 30.f, 86.0f, 1220.f, 646.0f); // 裁剪区域 (Clipping area)

    // 列表项绘制的X坐标常量 (X coordinate constant for list item drawing)
    static constexpr auto x = 90.f;
    // 列表项绘制的Y坐标偏移量 (Y coordinate offset for list item drawing)
    auto y = this->yoff;

    // 遍历并绘制应用列表项 (Iterate and draw application list items)
    for (size_t i = this->start; i < this->entries.size(); i++) {     
        // 检查是否为当前光标选中的项 (Check if this is the currently cursor-selected item)
        if (i == this->index) {
            // 当前选中项：绘制彩色边框和黑色背景 (Current selected item: draw colored border and black background)
            auto col = pulse.col;  // 获取脉冲颜色 (Get pulse color)
            col.r /= 255.f;        // 红色通道归一化(0-1范围) (Normalize red channel to 0-1 range)
            col.g /= 255.f;        // 绿色通道归一化 (Normalize green channel)
            col.b /= 255.f;        // 蓝色通道归一化 (Normalize blue channel)
            col.a = 1.f;           // 设置不透明度为1(完全不透明) (Set alpha to 1 - fully opaque)
            update_pulse_colour(); // 更新脉冲颜色(产生闪烁效果) (Update pulse color for blinking effect)
            // 绘制选中项的彩色边框 (Draw colored border for selected item)
            gfx::drawRect(this->vg, x - 5.f, y - 5.f, box_width + 10.f, box_height + 10.f, col);
            // 绘制选中项的黑色背景 (Draw black background for selected item)
            gfx::drawRect(this->vg, x, y, box_width, box_height, gfx::Colour::BLACK);
        }

        // 检查是否为用户已选择删除的项 (Check if this item is selected for deletion by user)
        if (this->entries[i].selected) {
            // 已选择项：绘制选择标记 (Selected item: draw selection marker)
            // 在列表项左侧绘制青色勾选图标 (Draw cyan checkmark icon on the left side of the list item)
            gfx::drawText(this->vg, x - 60.f, y + (box_height / 2.f) - (48.f / 2), 48.f, "\ue14b", nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
        }

        // 绘制列表项顶部和底部的分隔线 (Draw top and bottom separator lines for list item)
        gfx::drawRect(this->vg, x, y, box_width, 1.f, gfx::Colour::DARK_GREY);
        gfx::drawRect(this->vg, x, y + box_height, box_width, 1.f, gfx::Colour::DARK_GREY);

        // 创建并绘制应用图标 (Create and draw application icon)
        const auto icon_paint = nvgImagePattern(this->vg, x + icon_spacing, y + icon_spacing, 90.f, 90.f, 0.f, this->entries[i].image, 1.f);
        gfx::drawRect(this->vg, x + icon_spacing, y + icon_spacing, 90.f, 90.f, icon_paint);

        // 保存当前绘图状态并设置文本裁剪区域 (Save current drawing state and set text clipping area)
        nvgSave(this->vg);
        nvgScissor(this->vg, x + title_spacing_left, y, 585.f, box_height); // clip
        // 绘制应用名称，防止文本溢出 (Draw application name, preventing text overflow)
        gfx::drawText(this->vg, x + title_spacing_left, y + title_spacing_top, 24.f, this->entries[i].name.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
        // 恢复之前保存的绘图状态 (Restore previously saved drawing state)
        nvgRestore(this->vg);

        // 定义绘制存储大小信息的lambda函数 (Define lambda function to draw storage size information)
        const auto draw_size = [&](float x_offset, size_t size, const char* name) {
            if (size == 0) {
                // 存储大小为0时显示"---" (Show "---" when storage size is 0)
                gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: -----", name);
            } else {
                if (size >= 1024 * 1024 * 1024) {
                    if (size >= 1024ULL * 1024ULL * 1024ULL * 100ULL) { // no decimal
                        // 大于100GB时显示整数GB (Show integer GB when larger than 100GB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.0f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024*1024), "GB");
                    } else { // use decimal
                        // 小于100GB时显示一位小数GB (Show one decimal place GB when less than 100GB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.1f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024*1024), "GB");
                    }
                } else {
                    if (size >= 1024 * 1024 * 100) { // no decimal
                        // 大于100MB时显示整数MB (Show integer MB when larger than 100MB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.0f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024), "MB");
                    } else { // use decimal
                        // 小于100MB时显示一位小数MB (Show one decimal place MB when less than 100MB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.1f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024), "MB");
                    }
                }
            }
        };

        
        // 绘制NAND存储大小信息 (Draw NAND storage size information)
        draw_size(0.f, this->entries[i].size_nand, storage_nand.c_str());
        // 绘制SD卡存储大小信息 (Draw SD card storage size information)
        draw_size(200.f, this->entries[i].size_sd, storage_sd.c_str());

        // 绘制应用总大小信息 (Draw application total size information)
        if (this->entries[i].size_total >= 1024 * 1024 * 1024) {
            if (this->entries[i].size_total >= 1024ULL * 1024ULL * 1024ULL * 100ULL) { // no decimal
                // 大于100GB时显示整数GB (Show integer GB when larger than 100GB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.0f GB", static_cast<float>(this->entries[i].size_total) / static_cast<float>(1024*1024*1024));
            } else { // use decimal
                // 小于100GB时显示一位小数GB (Show one decimal place GB when less than 100GB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.1f GB", static_cast<float>(this->entries[i].size_total) / static_cast<float>(1024*1024*1024));
            }
        } else {
            if (this->entries[i].size_total >= 1024 * 1024 * 100) { // no decimal
                // 大于100MB时显示整数MB (Show integer MB when larger than 100MB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.0f MB", static_cast<float>(this->entries[i].size_total) / static_cast<float>(1024*1024));
            } else { // use decimal
                // 小于100MB时显示一位小数MB (Show one decimal place MB when less than 100MB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.1f MB", static_cast<float>(this->entries[i].size_total) / static_cast<float>(1024*1024));
            }
        }

        // 更新Y坐标为下一项位置 (Update Y coordinate to next item position)
        y += box_height;

        // 超出可视区域时停止绘制 (Stop drawing when out of visible area)
        if ((y + box_height) > 646.f) {
            break;
        }
    }

    // 恢复NanoVG绘图状态 / Restore NanoVG drawing state
    nvgRestore(this->vg);

    // 绘制底部状态文本：已选择数量、删除数量、总数量
    // Draw bottom status text: selected count, delete count, total count
    gfx::drawTextArgs(this->vg, 55.f, 670.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE, selected_count.c_str(), this->delete_count, total_count.load());
    
    
    // 检查扫描状态，动态调整按钮颜色 / Check scan status and dynamically adjust UI element colors
    if (is_scan_running) {

          // 扫描中状态下，根据状态设置按钮颜色 / Set button colors based on scanning state
          // 普通按钮保持白色 / Normal buttons remain white
          gfx::Colour button_color = gfx::Colour::WHITE;
          // Plus和ZR按钮在扫描时变灰，扫描完成后恢复白色 / Plus and ZR buttons turn grey during scan, restore white when complete
          gfx::Colour plus_zr_color = this->is_scan_running ? gfx::Colour::GREY : gfx::Colour::WHITE;

          // 按照drawButtons函数的方式绘制按钮 (Draw buttons following drawButtons function style)
          nvgTextAlign(this->vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP); // 设置文本对齐方式 (Set text alignment)
          float x = 1220.f; // 起始X坐标 (Starting X coordinate)
          const float y = 675.f; // 固定Y坐标 (Fixed Y coordinate)
          float bounds[4]{};

          // 定义所有按钮数组 (Define all buttons array)
          std::array<gfx::pair, 6> buttons = {
              gfx::pair{gfx::Button::A, button_select.c_str()},
              gfx::pair{gfx::Button::B, button_exit.c_str()},
              gfx::pair{gfx::Button::PLUS, button_delete_selected.c_str()},
              gfx::pair{gfx::Button::Y, this->GetSortStr()},
              gfx::pair{gfx::Button::ZR, button_invert_select.c_str()},
              this->delete_count == this->entries.size() ? gfx::pair{gfx::Button::ZL, button_deselect_all.c_str()} : gfx::pair{gfx::Button::ZL, button_select_all.c_str()}
          };

          // 遍历绘制所有按钮 (Iterate and draw all buttons)
          for (const auto& [button, text] : buttons) {
              // 根据按钮类型设置颜色 (Set color based on button type)
              gfx::Colour current_color;
              if (button == gfx::Button::PLUS || button == gfx::Button::Y || button == gfx::Button::ZL || button == gfx::Button::ZR) {
                  current_color = plus_zr_color;
              } else {
                  current_color = button_color;
              }
              nvgFillColor(this->vg, gfx::getColour(current_color));

              // 绘制按钮文本 (Draw button text)
              nvgFontSize(this->vg, 20.f);
              nvgTextBounds(this->vg, x, y, text, nullptr, bounds);
              auto text_len = bounds[2] - bounds[0];
              nvgText(this->vg, x, y, text, nullptr);

              // 向左移动文本宽度 (Move left by text width)
              x -= text_len + 10.f;

              // 绘制按钮图标 (Draw button icon)
              nvgFontSize(this->vg, 30.f);
              nvgTextBounds(this->vg, x, y - 7.f, gfx::getButton(button), nullptr, bounds);
              auto icon_len = bounds[2] - bounds[0];
              nvgText(this->vg, x, y - 7.f, gfx::getButton(button), nullptr);

              // 向左移动图标宽度 (Move left by icon width)
              x -= icon_len + 34.f;
          }
        
    } else {
        // 扫描完成，正常显示按钮 (Scan complete, display buttons normally)
        gfx::drawButtons(this->vg, 
            gfx::Colour::WHITE, 
            gfx::pair{gfx::Button::A, button_select.c_str()}, 
            gfx::pair{gfx::Button::B, button_exit.c_str()}, 
            gfx::pair{gfx::Button::PLUS, button_delete_selected.c_str()}, 
            gfx::pair{gfx::Button::Y, this->GetSortStr()}, 
            gfx::pair{gfx::Button::ZR, button_invert_select.c_str()}, 
            this->delete_count == this->entries.size() ? gfx::pair{gfx::Button::ZL, button_deselect_all.c_str()} : gfx::pair{gfx::Button::ZL, button_select_all.c_str()});
        
    }

}

// 绘制卸载确认界面 / Draw uninstall confirmation interface
void App::DrawConfirm() {

    // 根据删除线程状态动态设置B键文本 / Dynamically set B key text based on deletion thread status
    // 删除进行中显示"停止"，否则显示"返回" / Show "Stop" during deletion, otherwise show "Back"
    std::string b_button_text = (this->delete_thread.valid() && !this->finished_deleting) ? button_stop : button_back;
    
    // 绘制确认界面的操作按钮 / Draw operation buttons for confirmation interface
    // 在删除进行时将RIGHT+A和X按钮设置为灰色 / Set RIGHT+A and X buttons to gray during deletion
    bool is_deleting = this->delete_thread.valid() && !this->finished_deleting;
    
    gfx::drawButtons2Colored(this->vg, 
        gfx::make_pair2_colored(gfx::Button::RIGHT, gfx::Button::A, button_uninstalled.c_str(), is_deleting ? gfx::Colour::GREY : gfx::Colour::WHITE),
        gfx::make_pair2_colored(gfx::Button::B, b_button_text.c_str(), gfx::Colour::WHITE),
        gfx::make_pair2_colored(gfx::Button::X, button_remove.c_str(), is_deleting ? gfx::Colour::GREY : gfx::Colour::WHITE));
    
    // UI布局常量定义 / UI layout constants definition
    // 定义列表项的高度 (120像素) / Define list item height (120 pixels)
    constexpr auto box_height = 120.f;
    // 定义列表项的宽度 (715像素) / Define list item width (715 pixels)
    constexpr auto box_width = 715.f;
    // 定义图标与边框的间距 (12像素) / Define icon to border spacing (12 pixels)
    constexpr auto icon_spacing = 12.f;
    // 定义标题距离左侧的间距 (116像素) / Define title left spacing (116 pixels)
    constexpr auto title_spacing_left = 116.f;
    // 定义标题距离顶部的间距 (30像素) / Define title top spacing (30 pixels)
    constexpr auto title_spacing_top = 30.f;
    // 定义文本距离左侧的间距 (与标题左侧间距相同) / Define text left spacing (same as title)
    constexpr auto text_spacing_left = title_spacing_left;
    // 定义文本距离顶部的间距 (67像素) / Define text top spacing (67 pixels)
    constexpr auto text_spacing_top = 67.f;
    // 定义右侧信息框的X坐标 (870像素) / Define right sidebar X coordinate (870 pixels)
    constexpr auto sidebox_x = 870.f;
    // 定义右侧信息框的Y坐标 (87像素) / Define right sidebar Y coordinate (87 pixels)
    constexpr auto sidebox_y = 87.f;
    // 定义右侧信息框的宽度 (380像素) / Define right sidebar width (380 pixels)
    constexpr auto sidebox_w = 380.f;
    // 定义右侧信息框的高度 (558像素) / Define right sidebar height (558 pixels)
    constexpr auto sidebox_h = 558.f;

    // 计算准备删除的应用总占用容量 / Calculate total size of apps to be deleted
    double total_bytes = 0;

    // 绘制右侧信息框背景 / Draw right sidebar background
    gfx::drawRect(this->vg, sidebox_x, sidebox_y, sidebox_w, sidebox_h, gfx::Colour::LIGHT_BLACK);


    // 添加与LIST界面相同的存储条显示 (Add storage bars same as LIST interface)
    /**
     * @brief 绘制存储条的lambda函数
     * 
     * @param str 存储设备名称（如"系统内存"或"microSD卡"）
     * @param x 绘制起始X坐标
     * @param y 绘制起始Y坐标
     * @param storage_size 总存储大小
     * @param storage_free 可用存储大小
     * @param storage_used 已使用存储大小
     * @param app_size 当前应用占用大小
     */
    const auto draw_size = [&](const char* str, float x, float y, std::size_t storage_size, std::size_t storage_free, std::size_t storage_used, std::size_t app_size) {
        // 绘制存储设备名称文本
        gfx::drawText(this->vg, x, y-5.f, 22.f, str, nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
        // 绘制存储条白色外框
        gfx::drawRect(this->vg, x - 5.f, y + 28.f, 326.f, 16.f, gfx::Colour::WHITE);
        // 绘制存储条黑色背景
        gfx::drawRect(this->vg, x - 4.f, y + 29.f, 326.f - 2.f, 16.f - 2.f, gfx::Colour::LIGHT_BLACK);
        // 计算已使用存储条宽度
        const auto bar_width = (static_cast<float>(storage_used) / static_cast<float>(storage_size)) * (326.f - 4.f);
        // 计算当前应用占用存储条宽度
        const auto used_bar_width = (static_cast<float>(app_size) / static_cast<float>(storage_size)) * (326.f - 4.f);
        // 绘制已使用存储条(白色)
        gfx::drawRect(this->vg, x - 3.f, y + 30.f, bar_width, 16.f - 4.f, gfx::Colour::WHITE);
        // 绘制当前应用占用存储条(青色)
        gfx::drawRect(this->vg, x - 3.f + bar_width - used_bar_width, y + 30.f, used_bar_width, 16.f - 4.f, gfx::Colour::CYAN);
        // 绘制"可用空间"文本
        gfx::drawText(this->vg, x, y + 60.f, 20.f, space_available.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
        // 绘制可用空间大小(GB)
        gfx::drawTextArgs(this->vg, x + 315.f, y + 60.f, 24.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::WHITE, "%.1f GB", static_cast<float>(storage_free) / static_cast<float>(0x40000000));
        
    };
    

    // 遍历并绘制要卸载的应用列表项（只显示已选择的）
    // 首先更新selected_indices
    this->selected_indices.clear();
    std::size_t nand_size = 0;
    std::size_t sd_size = 0;
    std::size_t total_nand_size = 0;
    std::size_t total_sd_size = 0;
    for (size_t i = 0; i < this->entries.size(); i++) {
        if (this->entries[i].selected) {
            this->selected_indices.push_back(i);
            // 同时计算待删除应用在各存储设备上的总占用大小 (Calculate total size on each storage device)
            nand_size = this->entries[i].size_nand;
            sd_size = this->entries[i].size_sd;
            total_nand_size += this->entries[i].size_nand;
            total_sd_size += this->entries[i].size_sd;
        }
    }

    // 绘制系统内存存储条 (Draw system memory storage bar)
    draw_size(system_memory.c_str(), sidebox_x + 30.f, sidebox_y + 56.f, this->nand_storage_size_total, this->nand_storage_size_free, this->nand_storage_size_used, total_nand_size);
    // 绘制microSD卡存储条 (Draw microSD card storage bar)
    draw_size(micro_sd_card.c_str(), sidebox_x + 30.f, sidebox_y + 235.f, this->sdcard_storage_size_total, this->sdcard_storage_size_free, this->sdcard_storage_size_used, total_sd_size);
    

    // 保存当前绘图状态并设置裁剪区域
    nvgSave(this->vg);
    nvgScissor(this->vg, 30.f, 86.0f, 1220.f, 646.0f); // 裁剪区域

    static constexpr auto x = 90.f;
    auto y = this->yoff; // 初始y坐标
    
    
    for (size_t i = this->confirm_start; i < this->selected_indices.size(); i++) {
        const auto entry_index = this->selected_indices[i];
        if (entry_index >= this->entries.size()) continue; // 安全检查
        const auto& entry = this->entries[entry_index];
        
        if (i == this->confirm_index) {
            // 当前选中项：绘制彩色边框和黑色背景
            auto col = pulse.col;  // 获取脉冲颜色
            col.r /= 255.f;        // 红色通道归一化(0-1范围)
            col.g /= 255.f;        // 绿色通道归一化
            col.b /= 255.f;        // 蓝色通道归一化
            col.a = 1.f;           // 设置不透明度为1(完全不透明)
            update_pulse_colour(); // 更新脉冲颜色(产生闪烁效果)
            // 绘制选中项的彩色边框
            gfx::drawRect(this->vg, x - 5.f, y - 5.f, box_width + 10.f, box_height + 10.f, col);
            // 绘制选中项的黑色背景
            gfx::drawRect(this->vg, x, y, box_width, box_height, gfx::Colour::BLACK);
        }

        // 绘制列表项顶部和底部的分隔线 (Draw top and bottom separator lines for list item)
        gfx::drawRect(this->vg, x, y, box_width, 1.f, gfx::Colour::DARK_GREY);
        gfx::drawRect(this->vg, x, y + box_height, box_width, 1.f, gfx::Colour::DARK_GREY);

        // 创建并绘制应用图标 (Create and draw application icon)
        const auto icon_paint = nvgImagePattern(this->vg, x + icon_spacing, y + icon_spacing, 90.f, 90.f, 0.f, entry.image, 1.f);
        gfx::drawRect(this->vg, x + icon_spacing, y + icon_spacing, 90.f, 90.f, icon_paint);

        // 保存当前绘图状态并设置文本裁剪区域 (Save current drawing state and set text clipping area)
        nvgSave(this->vg);
        nvgScissor(this->vg, x + title_spacing_left, y, 585.f, box_height); // clip
        // 绘制应用名称，防止文本溢出 (Draw application name, preventing text overflow)
        gfx::drawText(this->vg, x + title_spacing_left, y + title_spacing_top, 24.f, entry.name.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE);
        // 恢复之前保存的绘图状态 (Restore previously saved drawing state)
        nvgRestore(this->vg);

        // 定义绘制存储大小信息的lambda函数 (Define lambda function to draw storage size information)
        const auto draw_size = [&](float x_offset, size_t size, const char* name) {
            if (size == 0) {
                // 存储大小为0时显示"---" (Show "---" when storage size is 0)
                gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: -----", name);
            } else {
                if (size >= 1024 * 1024 * 1024) {
                    if (size >= 1024ULL * 1024ULL * 1024ULL * 100ULL) { // no decimal
                        // 大于100GB时显示整数GB (Show integer GB when larger than 100GB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.0f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024*1024), "GB");
                    } else { // use decimal
                        // 小于100GB时显示一位小数GB (Show one decimal place GB when less than 100GB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.1f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024*1024), "GB");
                    }
                } else {
                    if (size >= 1024 * 1024 * 100) { // no decimal
                        // 大于100MB时显示整数MB (Show integer MB when larger than 100MB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.0f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024), "MB");
                    } else { // use decimal
                        // 小于100MB时显示一位小数MB (Show one decimal place MB when less than 100MB)
                        gfx::drawTextArgs(this->vg, x + text_spacing_left + x_offset, y + text_spacing_top + 9.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::SILVER, "%s: %.1f %s", name, static_cast<float>(size) / static_cast<float>(1024*1024), "MB");
                    }
                }
            }
        };

        // 绘制NAND存储大小信息 (Draw NAND storage size information)
        draw_size(0.f, entry.size_nand, storage_nand.c_str());
        // 绘制SD卡存储大小信息 (Draw SD card storage size information)
        draw_size(200.f, entry.size_sd, storage_sd.c_str());

        total_bytes += static_cast<double>(entry.size_total);


        // 绘制应用总大小信息 (Draw application total size information)
        if (entry.size_total >= 1024 * 1024 * 1024) {
            if (entry.size_total >= 1024ULL * 1024ULL * 1024ULL * 100ULL) { // no decimal
                // 大于100GB时显示整数GB (Show integer GB when larger than 100GB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.0f GB", static_cast<float>(entry.size_total) / static_cast<float>(1024*1024*1024));
            } else { // use decimal
                // 小于100GB时显示一位小数GB (Show one decimal place GB when less than 100GB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.1f GB", static_cast<float>(entry.size_total) / static_cast<float>(1024*1024*1024));
            }
        } else {
            if (entry.size_total >= 1024 * 1024 * 100) { // no decimal
                // 大于100MB时显示整数MB (Show integer MB when larger than 100MB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.0f MB", static_cast<float>(entry.size_total) / static_cast<float>(1024*1024));
            } else { // use decimal
                // 小于100MB时显示一位小数MB (Show one decimal place MB when less than 100MB)
                gfx::drawTextArgs(this->vg, x + 708.f, y + text_spacing_top + 2.f, 32.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN, "%.1f MB", static_cast<float>(entry.size_total) / static_cast<float>(1024*1024));
            }
        }

        // 更新Y坐标为下一项位置 (Update Y coordinate to next item position)
        y += box_height;

        // 超出可视区域时停止绘制 (Stop drawing when out of visible area)
        if ((y + box_height) > 646.f) {
            break;
        }
        
    }
    
    nvgRestore(this->vg);
    
    gfx::drawTextArgs(this->vg, 55.f, 670.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::WHITE, delete_selected_count.c_str(), this->delete_count);

    // 检查删除状态并显示相应信息 (Check deletion status and display corresponding information)
    std::scoped_lock lock{this->mutex};
    if (this->finished_deleting) {

        // 删除完成，在主列表区域显示完成信息 (Deletion completed, show completion message in main list area)
        gfx::drawTextBoxCentered(this->vg, 90.f, 130.f, 715.f, 516.f, 35.f, 1.5f, uninstalled_all_app.c_str(), nullptr, gfx::Colour::SILVER);
        if (deleted_nand_bytes > 0){
            gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 56.f + 85.f, 20.f, cumulative_released.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 56.f + 85.f, 24.f, (plus_sign + FormatStorageSize(deleted_nand_bytes)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
        }
        if (deleted_sd_bytes > 0){
            gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 235.f + 85.f, 20.f, cumulative_released.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 235.f + 85.f, 24.f, (plus_sign + FormatStorageSize(deleted_sd_bytes)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
        }
        
    } else if (this->delete_thread.valid() || this->deletion_interrupted) {
        // 正在删除中或删除被中断，显示删除进度 (Deleting in progress or interrupted, show deletion progress)
        if (this->deletion_interrupted) {
            if (total_nand_size > 0){
                gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 56.f + 85.f, 20.f, pending_total.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
                gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 56.f + 85.f, 24.f, (plus_sign + FormatStorageSize(total_nand_size)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            }
            if (total_sd_size > 0){
                gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 235.f + 85.f, 20.f, pending_total.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
                gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 235.f + 85.f, 24.f, (plus_sign + FormatStorageSize(total_sd_size)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            }
        
        } else {
            if (nand_size > 0){
                gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 56.f + 85.f, 20.f, space_releasing.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::RED);
                gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 56.f + 85.f, 24.f, (plus_sign + FormatStorageSize(nand_size)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::RED);
            }
            if (sd_size > 0){
                gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 235.f + 85.f, 20.f, space_releasing.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::RED);
                gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 235.f + 85.f, 24.f, (plus_sign + FormatStorageSize(sd_size)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::RED);
            }
        }
    
    } else {
        if (total_nand_size > 0){
            gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 56.f + 85.f, 20.f, pending_total.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 56.f + 85.f, 24.f, (plus_sign + FormatStorageSize(total_nand_size)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
        }
        if (total_sd_size > 0){
            gfx::drawText(this->vg, sidebox_x + 30.f, sidebox_y + 235.f + 85.f, 20.f, pending_total.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
            gfx::drawText(this->vg, sidebox_x + 30.f + 315.f, sidebox_y + 235.f + 85.f, 24.f, (plus_sign + FormatStorageSize(total_sd_size)).c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, gfx::Colour::CYAN);
        }
    }

    nvgSave(this->vg);

}


void App::Sort()
{
    switch (static_cast<SortType>(this->sort_type)) {
        case SortType::Size_BigSmall:
            // 按容量从大到小排序
            std::ranges::sort(this->entries, std::ranges::greater{}, &AppEntry::size_total);
            break;
        case SortType::Alphabetical:
            // 按应用名称A-Z排序
            std::ranges::sort(this->entries, {}, &AppEntry::name);
            break;
        default:
            // 默认按容量从大到小排序
            std::ranges::sort(this->entries, std::ranges::greater{}, &AppEntry::size_total);
            break;
    }
}

const char* App::GetSortStr() {
    switch (static_cast<SortType>(this->sort_type)) {
        case SortType::Size_BigSmall:
            // 返回按容量从大到小排序的提示文本
            return sort_size_bigsmall.c_str();
        case SortType::Alphabetical:
            // 返回按字母顺序排序的提示文本
            return sort_alpha_az.c_str();
        default:
            // 默认返回按容量从大到小排序的提示文本
            return sort_size_bigsmall.c_str();
    }
}

void App::UpdateLoad() {
    if (this->controller.B) {
        this->async_thread.request_stop();
        this->async_thread.get();
        this->quit = true;
        return;
    }

    {
        std::scoped_lock lock{this->mutex};
        // 只要有应用加载完成就立即显示列表，实现真正的立即显示
        // Show list immediately when any app is loaded, achieving true immediate display
        if (scanned_count.load() > 0 || finished_scanning) {
            if (finished_scanning) {
                this->async_thread.get();
            }
            this->Sort();
            // 排序后重置加载状态并触发图标重新加载
            // Reset load state after sorting and trigger icon reload
            this->last_loaded_range = {SIZE_MAX, SIZE_MAX};
            this->LoadVisibleAreaIcons();
            this->menu_mode = MenuMode::LIST;
        }
    }
}

void App::UpdateList() {
    // 扫描过程中禁用排序和删除功能
    if (this->controller.B) {
        this->quit = true;
    } else if (this->controller.A) { // 允许在扫描过程中选择列表项
            // 原代码: } else if (!is_scan_running && this->controller.A) { // 非扫描状态下才允许选择
        if (this->entries[this->index].selected) {
            this->entries[this->index].selected = false;
            this->delete_count--;
        } else {
            this->entries[this->index].selected = true;
            this->delete_count++;
        }
        // add to / remove from delete list
    } else if (!is_scan_running && this->controller.START) { // 非扫描状态下才允许删除
        for (const auto&p : this->entries) {
            if (p.selected) {
                this->delete_entries.push_back(p.id);
            }
        }
        if (this->delete_entries.size()) {
            // 保存列表界面的光标位置到确认界面变量
            this->confirm_index = 0;
            this->confirm_start = 0;
            this->ypos = this->yoff = 130.f;
            
            // 重置删除相关状态，确保右侧信息窗口显示确认删除文本而不是进度条
            // Reset deletion-related states to ensure right info window shows confirmation text instead of progress bar
            {
                std::scoped_lock lock{this->mutex};
                this->finished_deleting = false;
                this->deletion_interrupted = false;
            }
            
            // 如果之前的删除线程仍然有效，等待其完成并重置
            // If previous deletion thread is still valid, wait for completion and reset
            if (this->delete_thread.valid()) {
                this->delete_thread.get();
            }
            
            this->menu_mode = MenuMode::CONFIRM;
        }
    } else if (this->controller.DOWN) { // move down
        if (this->index < (this->entries.size() - 1)) {
            this->index++;
            this->ypos += this->BOX_HEIGHT;
            if ((this->ypos + this->BOX_HEIGHT) > 646.f) {
                LOG("moved down\n");
                this->ypos -= this->BOX_HEIGHT;
                this->yoff = this->ypos - ((this->index - this->start - 1) * this->BOX_HEIGHT);
                this->start++;
            }
            // 光标移动后触发视口感知图标加载
            // Trigger viewport-aware icon loading after cursor movement
            this->LoadVisibleAreaIcons();
        }
    } else if (this->controller.UP) { // move up
        if (this->index != 0 && this->entries.size()) {
            this->index--;
            this->ypos -= this->BOX_HEIGHT;
            if (this->ypos < 86.f) {
                LOG("moved up\n");
                this->ypos += this->BOX_HEIGHT;
                this->yoff = this->ypos;
                this->start--;
            }
            // 光标移动后触发视口感知图标加载
            // Trigger viewport-aware icon loading after cursor movement
            this->LoadVisibleAreaIcons();
        }
    } else if (!is_scan_running && this->controller.Y) { // 非扫描状态下才允许排序
        this->sort_type++;

        if (this->sort_type == std::to_underlying(SortType::MAX)) {
            this->sort_type = 0;
        }

        this->Sort();
        // 排序后重置加载状态并触发图标重新加载
        // Reset load state after sorting and trigger icon reload
        this->last_loaded_range = {SIZE_MAX, SIZE_MAX};
        this->LoadVisibleAreaIcons();
        // 重置选择索引，使选择框回到第一项
        this->index = 0;
        // 重置滚动位置
        this->ypos = this->yoff = 130.f;
        this->start = 0;

    } else if (!is_scan_running && this->controller.L2) { // 非扫描状态下才允许全选/取消全选
        if (this->delete_count == this->entries.size()) {
            for (auto& a : this->entries) {
                a.selected = false;
            }
            this->delete_count = 0;
        } else {
            for (auto& a : this->entries) {
                a.selected = true;
            }
            this->delete_count = this->entries.size();
        }
    } else if (!is_scan_running && this->controller.R2) { // 非扫描状态下才允许反选
        for (auto& a : this->entries) {
            a.selected = !a.selected;
        }
        // 更新delete_count
        this->delete_count = 0;
        for (const auto& a : this->entries) {
            if (a.selected) {
                this->delete_count++;
            }
        }
    } else if (this->controller.L) { // L键向上翻页 (L key for page up)
        if (this->entries.size() > 0) {
            // 直接更新index，向上翻页4个位置 (Directly update index, page up 4 positions)
            if (this->index >= 4) {
                this->index -= 4;
            } else {
                this->index = 0;
            }
            
            // 直接更新start位置，确保翻页显示4个项目 (Directly update start position to ensure 4 items per page)
            if (this->start >= 4) {
                this->start -= 4;
            } else {
                this->start = 0;
            }
            
            // 边界检查：确保index和start的一致性 (Boundary check: ensure consistency between index and start)
            if (this->index < this->start) {
                this->index = this->start;
            }
            
            // 边界检查：确保index不会超出当前页面范围 (Boundary check: ensure index doesn't exceed current page range)
            std::size_t max_index_in_page = this->start + 3; // 当前页面最大索引 (Maximum index in current page)
            if (this->index > max_index_in_page && max_index_in_page < this->entries.size()) {
                this->index = max_index_in_page;
            }
            
            // 更新相关显示变量 (Update related display variables)
            this->ypos = 130.f + (this->index - this->start) * this->BOX_HEIGHT;
            this->yoff = 130.f;
            
            // 翻页后触发视口感知图标加载 (Trigger viewport-aware icon loading after page change)
            this->LoadVisibleAreaIcons();
        }
    } else if (this->controller.R) { // R键向下翻页 (R key for page down)
        if (this->entries.size() > 0) {
            // 直接更新index，向下翻页4个位置 (Directly update index, page down 4 positions)
            this->index += 4;
            
            // 边界检查：确保不超出列表范围 (Boundary check: ensure not exceeding list range)
            if (this->index >= this->entries.size()) {
                this->index = this->entries.size() - 1;
            }
            
            // 直接更新start位置，确保翻页显示4个项目 (Directly update start position to ensure 4 items per page)
            this->start += 4;
            
            // 边界检查：确保start不会超出合理范围 (Boundary check: ensure start doesn't exceed reasonable range)
            if (this->entries.size() > 4) {
                std::size_t max_start = this->entries.size() - 4;
                if (this->start > max_start) {
                    this->start = max_start;
                    // 当到达末尾时，调整index到最后一个可见项 (When reaching end, adjust index to last visible item)
                    this->index = this->entries.size() - 1;
                }
            } else {
                this->start = 0;
            }
            
            // 更新相关显示变量 (Update related display variables)
            this->ypos = 130.f + (this->index - this->start) * this->BOX_HEIGHT;
            this->yoff = 130.f;
            
            // 翻页后触发视口感知图标加载 (Trigger viewport-aware icon loading after page change)
            this->LoadVisibleAreaIcons();
        }
    } 

    // else if (this->controller.DOWN) { // move down
    //     if (this->index < (this->entries.size() - 1)) {
    //         this->index++;
    //         this->ypos += this->BOX_HEIGHT;
    //         if ((this->ypos + this->BOX_HEIGHT) > 646.f) {
    //             LOG("moved down\n");
    //             this->ypos -= this->BOX_HEIGHT;
    //             this->yoff = this->ypos - ((this->index - this->start - 1) * this->BOX_HEIGHT);
    //             this->start++;
    //         }
    //         // 光标移动后触发视口感知图标加载
    //         // Trigger viewport-aware icon loading after cursor movement
    //         this->LoadVisibleAreaIcons();
    //     }
    // handle direction keys
}

void App::UpdateConfirm() {
    
    // 使用可见区域图标加载，避免内存浪费 (Use visible area icon loading to avoid memory waste)
    this->LoadConfirmVisibleAreaIcons();
    
    // 检查删除线程状态，防止并发删除操作 (Check deletion thread status to prevent concurrent deletion operations)
    if (this->controller.RIGHT_AND_A && !this->delete_entries.empty() && 
        (!this->delete_thread.valid() || this->finished_deleting)) {
        this->finished_deleting = false;
        this->delete_index = 0;
        
        // 计算准备删除的应用总占用容量 (Calculate total size of apps to be deleted)
        this->deleted_nand_bytes = 0.0;
        this->deleted_sd_bytes = 0.0;
        this->deleted_app_count = this->delete_entries.size();
        for (const auto& app_id : this->delete_entries) {
            for (const auto& entry : this->entries) {
                if (entry.id == app_id) {
                    this->deleted_nand_bytes += static_cast<double>(entry.size_nand);
                    this->deleted_sd_bytes += static_cast<double>(entry.size_sd);
                    break;
                }
            }
        }
        NsDeleteData data{
            .entries = this->delete_entries,
            .del_cb = [this](bool error){
                    std::scoped_lock lock{this->mutex};
                    if (error) {
                        LOG("error whilst deleting AppID %lX\n", this->delete_entries[this->delete_index]);
                    } else {
                        // 删除成功，从主应用列表中移除该应用 (Deletion successful, remove from main app list)
                        const auto app_id = this->delete_entries[this->delete_index];
                        for (size_t i = 0; i < this->entries.size(); i++) {
                            if (this->entries[i].id == app_id) {
                                nvgDeleteImage(this->vg, this->entries[i].image);
                                this->entries.erase(this->entries.begin() + i);
                                break;
                            }
                        }
                        // 更新删除计数和应用总数 (Update delete count and total count)
                        this->delete_count--;
                        total_count.fetch_sub(1);
                    }
                    this->delete_index++;
                },
            .done_cb = [this](){
                std::scoped_lock lock{this->mutex};
                LOG("finished deleting entries...\n");
                this->finished_deleting = true;
                
                // 重置状态 (Reset state)
                // delete_count已经在del_cb中逐个减少，这里直接设为0 (delete_count already decreased in del_cb, set to 0 here)
                this->delete_count = 0;
                this->ypos = this->yoff = 130.f;
                this->index = 0;
                this->start = 0;
                this->delete_entries.clear();
            }
        };
        // 保持在CONFIRM界面进行删除，不跳转到PROGRESS界面 (Stay in CONFIRM interface for deletion, don't jump to PROGRESS)
        this->delete_thread = util::async(NsDeleteAppsAsync, data);
    } else if (this->controller.B) {
        // 检查删除状态来决定B键行为 (Check deletion status to determine B key behavior)
        if (this->delete_thread.valid() && !this->finished_deleting) {
            // 删除进行中，B键中断删除 (Deletion in progress, B key interrupts deletion)
            this->delete_thread.request_stop();
            // 等待删除线程结束 (Wait for deletion thread to finish)
            if (this->delete_thread.valid()) {
                this->delete_thread.get();
            }
            // 检查删除是否实际已完成 (Check if deletion actually completed)
            {
                std::scoped_lock lock{this->mutex};
                if (this->finished_deleting) {
                    // 删除实际已完成，不标记为中断 (Deletion actually completed, don't mark as interrupted)
                    // finished_deleting保持true，让后续逻辑正确处理 (Keep finished_deleting true for proper handling)
                } else {
                    // 删除被真正中断 (Deletion was truly interrupted)
                    this->finished_deleting = false;
                    this->deletion_interrupted = true; // 标记发生了中断 (Mark that interruption occurred)
                }
            }
        } else {
            // 未开始删除或删除已完成，B键返回列表 (Not started or deletion completed, B key returns to list)
            this->delete_entries.clear();
            
            // 检查是否删除已完成 (Check if deletion is completed)
            bool deletion_completed = this->finished_deleting;
            
            // 根据是否发生过中断或删除已完成来决定是否重置光标位置 (Decide whether to reset cursor position based on interruption or completion)
            if (this->deletion_interrupted || deletion_completed) {
                // 发生过中断或删除已完成，重置应用列表的光标位置 (Interruption occurred or deletion completed, reset app list cursor position)
                this->index = 0;
                this->start = 0;
                this->ypos = 130.f; // 重置到顶部位置 (Reset to top position)
                this->yoff = 130.f; // 重置yoff
                
                // 重置最开始应用列表的所有选中状态 (Reset all selection states in the original app list)
                for (auto& entry : this->entries) {
                    entry.selected = false;
                }
                this->delete_count = 0; // 重置删除计数 (Reset delete count)
                
                // 重置中断和完成标志 (Reset interruption and completion flags)
                this->deletion_interrupted = false;
                this->finished_deleting = false;
            } else {
                // 直接按B返回，保持原有光标位置 (Direct B return, maintain original cursor position)
                this->ypos = 130.f + (this->index - this->start) * this->BOX_HEIGHT; // 重新计算ypos
                this->yoff = 130.f; // 重置yoff
            }
            
            // 重置删除完成标志 (Reset deletion finished flag)
            {
                std::scoped_lock lock{this->mutex};
                this->finished_deleting = false;
            }
            
            // 返回列表时强制重置加载状态并触发图标重新加载，确保图标立即显示 (Force reset load state and trigger icon reload when returning to list to ensure immediate icon display)
            this->last_loaded_range = {SIZE_MAX, SIZE_MAX}; // 强制重置加载范围 (Force reset load range)
            this->last_load_time = std::chrono::steady_clock::time_point{}; // 重置防抖时间 (Reset debounce time)
            this->LoadVisibleAreaIcons();
            
            this->menu_mode = MenuMode::LIST;
        }
    } else if (this->controller.L) { // L键向上翻页 (L key for page up)
        if (this->selected_indices.size() > 0) {
            // 直接更新confirm_index，向上翻页4个位置 (Directly update confirm_index, page up 4 positions)
            if (this->confirm_index >= 4) {
                this->confirm_index -= 4;
            } else {
                this->confirm_index = 0;
            }
            
            // 直接更新confirm_start位置，确保翻页显示4个项目 (Directly update confirm_start position to ensure 4 items per page)
            if (this->confirm_start >= 4) {
                this->confirm_start -= 4;
            } else {
                this->confirm_start = 0;
            }
            
            // 边界检查：确保confirm_index和confirm_start的一致性 (Boundary check: ensure consistency between confirm_index and confirm_start)
            if (this->confirm_index < this->confirm_start) {
                this->confirm_index = this->confirm_start;
            }
            
            // 边界检查：确保confirm_index不会超出当前页面范围 (Boundary check: ensure confirm_index doesn't exceed current page range)
            std::size_t max_index_in_page = this->confirm_start + 3; // 当前页面最大索引 (Maximum index in current page)
            if (this->confirm_index > max_index_in_page && max_index_in_page < this->selected_indices.size()) {
                this->confirm_index = max_index_in_page;
            }

            // 翻页后触发视口感知图标加载 (Trigger viewport-aware icon loading after page change)
            this->LoadVisibleAreaIcons();
            
        }

    } else if (this->controller.R) { // R键向下翻页 (R key for page down)
        if (this->selected_indices.size() > 0) {
            // 直接更新confirm_index，向下翻页4个位置 (Directly update confirm_index, page down 4 positions)
            this->confirm_index += 4;
            
            // 边界检查：确保不超出列表范围 (Boundary check: ensure not exceeding list range)
            if (this->confirm_index >= this->selected_indices.size()) {
                this->confirm_index = this->selected_indices.size() - 1;
            }
            
            // 直接更新confirm_start位置，确保翻页显示4个项目 (Directly update confirm_start position to ensure 4 items per page)
            this->confirm_start += 4;
            
            // 边界检查：确保confirm_start不会超出合理范围 (Boundary check: ensure confirm_start doesn't exceed reasonable range)
            if (this->selected_indices.size() > 4) {
                std::size_t max_start = this->selected_indices.size() - 4;
                if (this->confirm_start > max_start) {
                    this->confirm_start = max_start;
                    // 当到达末尾时，调整confirm_index到最后一个可见项 (When reaching end, adjust confirm_index to last visible item)
                    this->confirm_index = this->selected_indices.size() - 1;
                }
            } else {
                this->confirm_start = 0;
            }

            // 翻页后触发视口感知图标加载 (Trigger viewport-aware icon loading after page change)
            this->LoadVisibleAreaIcons();
        }

    } else if (this->controller.UP) {// 上键: 向上滚动列表 (Up key: Scroll list up)
        if (this->confirm_index > 0) {
            this->confirm_index--;
            // 滚动处理
            if (this->confirm_index < this->confirm_start) {
                this->confirm_start = this->confirm_index;
            }
        }
    }else if (this->controller.DOWN) {// 下键: 向下滚动列表 (Down key: Scroll list down)
        if (this->confirm_index < this->selected_indices.size() - 1) {
            this->confirm_index++;
            // 滚动处理
            if ((this->confirm_index - this->confirm_start) >= 4) { // 假设每页显示4个项目
                this->confirm_start++;
            }
        }
    } else if (this->controller.X) { // X键: 从待删除列表中移除当前光标所在的应用 (X key: Remove current cursor app from delete list)
        // 检查删除状态，如果正在删除则禁用X键功能 (Check deletion status, disable X key if deletion is in progress)
        if (this->delete_thread.valid() && !this->finished_deleting) {
            // 删除进行中，禁用X键功能 (Deletion in progress, disable X key functionality)
            return;
        }
        
        if (!this->selected_indices.empty() && this->confirm_index < this->selected_indices.size()) {
            // 获取当前光标所在的应用索引 (Get current cursor app index)
            size_t app_index = this->selected_indices[this->confirm_index];
            
            // 从主应用列表中取消选中状态 (Unselect from main app list)
            if (app_index < this->entries.size()) {
                this->entries[app_index].selected = false;
                this->delete_count--;
                
                // 从待删除列表中移除该应用ID (Remove app ID from delete list)
                u64 app_id = this->entries[app_index].id;
                auto it = std::find(this->delete_entries.begin(), this->delete_entries.end(), app_id);
                if (it != this->delete_entries.end()) {
                    this->delete_entries.erase(it);
                }
            }
            
            // 从选中索引列表中移除 (Remove from selected indices list)
            this->selected_indices.erase(this->selected_indices.begin() + this->confirm_index);
            
            // 调整光标位置 (Adjust cursor position)
            if (this->selected_indices.empty()) {
                // 如果没有选中的应用了，返回主列表 (If no selected apps, return to main list)
                this->menu_mode = MenuMode::LIST;
                this->confirm_index = 0;
                this->confirm_start = 0;
            } else {
                // 调整光标位置，确保不超出范围 (Adjust cursor position to stay within range)
                if (this->confirm_index >= this->selected_indices.size()) {
                    this->confirm_index = this->selected_indices.size() - 1;
                }
                
                // 调整显示起始位置 (Adjust display start position)
                if (this->confirm_index < this->confirm_start) {
                    this->confirm_start = this->confirm_index;
                } else if (this->confirm_index >= this->confirm_start + 4) {
                    this->confirm_start = this->confirm_index - 3;
                    if (this->confirm_start < 0) this->confirm_start = 0;
                }
            }
        }
    }
}



/**
 * @brief 获取所有应用程序ID
 * @param app_ids 存储应用程序ID的向量引用
 * @return 成功返回0，失败返回错误码
 * 
 * 此函数通过分页调用nsListApplicationRecord获取所有应用程序ID，
 * 支持任意数量的应用，不受缓冲区大小限制。
 */
Result App::GetAllApplicationIds(std::vector<u64>& app_ids) {
    app_ids.clear();

    // 定义每页获取的应用记录数量
    constexpr size_t PAGE_SIZE = 30;
    std::array<NsApplicationRecord, PAGE_SIZE> record_list;
    s32 record_count = 0;
    s32 offset = 0;
    Result result = 0;

    // 分页获取应用记录，直到获取不到新记录为止
    do {
        // 调用nsListApplicationRecord获取一页应用记录
        result = nsListApplicationRecord(record_list.data(), static_cast<s32>(record_list.size()), offset, &record_count);

        // 检查是否获取成功
        if (R_FAILED(result)) {
            return result;
        }

        // 如果没有获取到记录，退出循环
        if (record_count <= 0) {
            break;
        }

        // 确保记录数量不超过缓冲区大小
        record_count = std::min(record_count, static_cast<s32>(record_list.size()));

        // 将获取到的应用ID添加到向量中
        for (s32 i = 0; i < record_count; i++) {
            app_ids.push_back(record_list[i].application_id);
        }

        // 更新偏移量，准备获取下一页
        offset += record_count;
    } while (record_count > 0);

    return 0;
}


// 快速获取应用基本信息并缓存图标数据
// Fast get application basic info and cache icon data
bool App::TryGetAppBasicInfoWithIconCache(u64 application_id, AppEntry& entry) {
    
    // 1. 优先尝试从缓存获取应用名称
    // 1. Try to get application name from cache first
    NxTitleCacheApplicationMetadata* cached_metadata = nxtcGetApplicationMetadataEntryById(application_id);
    if (cached_metadata != nullptr) {
        entry.name = cached_metadata->name;
        entry.id = cached_metadata->title_id;
        // 暂时设置默认值，稍后异步加载
        // Set default values temporarily, load asynchronously later
        entry.size_total = entry.size_nand = entry.size_sd = 0;
        entry.image = this->default_icon_image;
        entry.own_image = false;
        
        // 缓存图标数据到AppEntry中
        // Cache icon data in AppEntry
        if (cached_metadata->icon_data && cached_metadata->icon_size > 0) {
            entry.cached_icon_data.resize(cached_metadata->icon_size);
            memcpy(entry.cached_icon_data.data(), cached_metadata->icon_data, cached_metadata->icon_size);
            entry.has_cached_icon = true;
        } else {
            entry.has_cached_icon = false;
        }
        
        nxtcFreeApplicationMetadata(&cached_metadata);
        
        return true;
    }
    
    // 2. 缓存获取失败，尝试从NS服务获取（仅名称）
    // 2. Cache failed, try to get from NS service (name only)
    auto control_data = std::make_unique<NsApplicationControlData>();
    u64 jpeg_size{};
    
    Result result = nsGetApplicationControlData(NsApplicationControlSource_Storage, application_id, control_data.get(), sizeof(NsApplicationControlData), &jpeg_size);
    if (R_FAILED(result)) {
        return false;
    }
    
    int systemlanguage = tj::LangManager::getInstance().getCurrentLanguage();
    NacpLanguageEntry* language_entry = &control_data->nacp.lang[systemlanguage];
    
    // 如果当前语言条目为空，则遍历查找第一个非空条目
    // If the current language entry is empty, iterate to find the first non-empty entry
    if (language_entry->name[0] == '\0' && language_entry->author[0] == '\0') {
        for (int i = 0; i < 16; i++) {
            if (control_data->nacp.lang[i].name[0] != '\0' || control_data->nacp.lang[i].author[0] != '\0') {
                language_entry = &control_data->nacp.lang[i];
                break;
            }
        }
    }
    
    entry.name = language_entry->name;
    entry.id = application_id;
    // 暂时设置默认值，稍后异步加载
    // Set default values temporarily, load asynchronously later
    entry.size_total = entry.size_nand = entry.size_sd = 0;
    entry.image = this->default_icon_image;
    entry.own_image = false;
    
    // 缓存图标数据到AppEntry中，避免后续重复读取
    // Cache icon data in AppEntry to avoid repeated reads later
    if (jpeg_size > sizeof(NacpStruct)) {
        size_t icon_size = jpeg_size - sizeof(NacpStruct);
        entry.cached_icon_data.resize(icon_size);
        memcpy(entry.cached_icon_data.data(), control_data->icon, icon_size);
        entry.has_cached_icon = true;
        
        // 仍然添加到缓存系统以供其他用途
        // Still add to cache system for other uses
        nxtcAddEntry(application_id, &control_data->nacp, icon_size, control_data->icon, true);
    } else {
        entry.has_cached_icon = false;
    }
    
    return true;
}




// 获取应用占用大小信息
// Get application occupied size information
void App::GetAppSizeInfo(u64 application_id, AppEntry& entry) {
    ApplicationOccupiedSize size{};
    Result result = nsCalculateApplicationOccupiedSize(application_id, (NsApplicationOccupiedSize*)&size);
    if (R_FAILED(result)) {
        LOG("获取应用占用空间失败，ID: %lX\n", application_id);
        entry.size_total = entry.size_nand = entry.size_sd = 0;
        return;
    }

    // 填充应用大小信息的lambda函数
    // Lambda function to fill application size information
    auto fill_size = [&](const ApplicationOccupiedSizeEntry& e) {
        switch (e.storageId) {
            case NcmStorageId_BuiltInUser: // 内置存储
                entry.size_nand = e.sizeApplication + e.sizeAddOnContent + e.sizePatch;
                break;
            case NcmStorageId_SdCard: // SD卡存储
                entry.size_sd = e.sizeApplication + e.sizeAddOnContent + e.sizePatch;
                break;
            default:
                entry.size_total = entry.size_nand = entry.size_sd = 0;
                break;
        }
    };

    // 填充存储大小信息
    // Fill storage size information
    fill_size(size.entry[0]);
    fill_size(size.entry[1]);

    // 计算总大小
    // Calculate total size
    entry.size_total = entry.size_nand + entry.size_sd;
}




// 分离式扫描：第一阶段快速扫描应用名称和大小
// Separated scanning: Phase 1 - Fast scan application names
void App::FastScanNames(std::stop_token stop_token) {
    // 标记扫描开始
    is_scan_running = true;
    // 初始化扫描计数器
    scanned_count = 0;

    // 结果变量，用于存储系统调用的返回值
    Result result{};
    // 计数器，用于统计找到的应用总数
    size_t count{};

    // 初始化libnxtc库
    if (!nxtcInitialize()) {
        LOG("初始化libnxtc库失败\n");
    }

    // 获取所有应用ID
    std::vector<u64> app_ids{};
    result = GetAllApplicationIds(app_ids);
    // 获取应用总数量
    total_count.store(app_ids.size());
    if (R_FAILED(result)) {
        LOG("获取应用ID失败\n");
        goto done;
    }

    // 如果没有应用
    if (app_ids.empty()) {
        LOG("应用记录数为0\n");
        goto done;
    }

    // 快速遍历所有应用ID，仅获取名称
    // Fast iterate through all application IDs, get names only
    for (u64 application_id : app_ids) {
        if (stop_token.stop_requested()) break;

        // 应用条目，用于存储单个应用的信息
        AppEntry entry;
        bool is_corrupted = false;
        
        // 1. 快速获取应用名称和大小信息（不包含图标）
        // 1. Fast get application name and size info (excluding icon)
        if (!TryGetAppBasicInfoWithIconCache(application_id, entry)) {
            is_corrupted = true;
        } else {
            // 2. 立即加载大小信息（因为需要支持按大小排序）
            // 2. Immediately load size info (needed for size-based sorting)
            GetAppSizeInfo(application_id, entry);
        }

        // 标记是否存在损坏的安装
        // Mark if there is a corrupted installation
        if (is_corrupted) {
            // 损坏的安装处理
            // Handle corrupted installation
            entry.name = corrupted_install.c_str();
            entry.id = application_id;
            entry.image = this->default_icon_image;
            entry.own_image = false;
            entry.size_total = entry.size_nand = entry.size_sd = 0;
        }

        // 加锁保护entries列表
        {
            std::scoped_lock lock{entries_mutex};
            // 将应用条目添加到列表
            this->entries.emplace_back(std::move(entry));
            // 更新扫描计数器
            scanned_count++;
            count++;
        }

        // 为首屏4个应用立即提交最高优先级图标加载任务
        // Immediately submit highest priority icon loading tasks for first screen 4 apps
        if (count <= BATCH_SIZE && !is_corrupted && entry.has_cached_icon) {
            ResourceLoadTask icon_task;
            icon_task.application_id = application_id;
            icon_task.priority = 0; // 最高优先级 (Highest priority)
            icon_task.submit_time = std::chrono::steady_clock::now();
            icon_task.task_type = ResourceTaskType::ICON;
            
            icon_task.load_callback = [this, application_id]() {
                std::vector<unsigned char> icon_data;
                bool has_icon_data = false;
                
                // 获取缓存的图标数据
                // Get cached icon data
                {
                    std::scoped_lock lock{entries_mutex};
                    auto it = std::find_if(entries.begin(), entries.end(),
                        [application_id](const AppEntry& entry) {
                            return entry.id == application_id && entry.has_cached_icon;
                        });
                    
                    if (it != entries.end()) {
                        icon_data = it->cached_icon_data;
                        has_icon_data = true;
                    }
                }
                
                // 验证并创建图像
                // Validate and create image
                if (has_icon_data && !icon_data.empty() && IsValidJpegData(icon_data)) {
                    int image_id = nvgCreateImageMem(this->vg, 0, icon_data.data(), icon_data.size());
                    if (image_id > 0) {
                        std::scoped_lock lock{entries_mutex};
                        auto it = std::find_if(entries.begin(), entries.end(),
                            [application_id](const AppEntry& entry) {
                                return entry.id == application_id;
                            });
                        
                        if (it != entries.end()) {
                            // 如果之前有自己的图像，先删除
                            // If previously had own image, delete it first
                            if (it->own_image && it->image != this->default_icon_image) {
                                nvgDeleteImage(this->vg, it->image);
                            }
                            it->image = image_id;
                            it->own_image = true;
                        }
                    }
                }
            };
            
            resource_manager.submitLoadTask(icon_task);
        }

        // 首批应用名称加载完成标记
        // Mark first batch names loading complete
        if (scanned_count.load() == BATCH_SIZE) {
            initial_batch_loaded = true;
        }

        //线程休息1ms避免卡死（比原来更快）
        svcSleepThread(1000000ULL);
    }

    // 结束标签
    done:
    // 标记扫描结束
    is_scan_running = false;

    // 刷新缓存文件
    nxtcFlushCacheFile();

    // 退出libnxtc库
    nxtcExit();

    // 加锁保护finished_scanning
    std::scoped_lock lock{this->mutex};

    // 标记扫描完成
    this->finished_scanning = true;
}


// 计算当前可见区域的应用索引范围
// Calculate the index range of applications in current visible area
std::pair<size_t, size_t> App::GetVisibleRange() const {
    std::scoped_lock lock{entries_mutex};
    
    if (entries.empty()) {
        return {0, 0};
    }
    
    // 屏幕上最多显示4个应用项
    // Maximum 4 application items can be displayed on screen
    constexpr size_t MAX_VISIBLE_ITEMS = 4;
    
    size_t visible_start = this->start;
    size_t visible_end = std::min(visible_start + MAX_VISIBLE_ITEMS, entries.size());
    
    return {visible_start, visible_end};
}

// 获取卸载界面的可见范围 (Get visible range for confirm interface)
std::pair<size_t, size_t> App::GetConfirmVisibleRange() const {
    if (selected_indices.empty()) {
        return {0, 0};
    }
    
    // 屏幕上最多显示4个应用项
    // Maximum 4 application items can be displayed on screen
    constexpr size_t MAX_VISIBLE_ITEMS = 4;
    
    size_t visible_start = this->confirm_start;
    size_t visible_end = std::min(visible_start + MAX_VISIBLE_ITEMS, selected_indices.size());
    
    return {visible_start, visible_end};
}

// 基于视口的智能图标加载：根据光标位置优先加载可见区域的图标
// Viewport-aware smart icon loading: prioritize loading icons in visible area based on cursor position

void App::LoadVisibleAreaIcons() {
    auto now = std::chrono::steady_clock::now();
    
    // 防抖：如果距离上次调用不足100ms，则跳过
    // Debouncing: skip if less than 100ms since last call
    constexpr auto DEBOUNCE_INTERVAL = std::chrono::milliseconds(100);
    if (now - last_load_time < DEBOUNCE_INTERVAL) {
        return;
    }
    last_load_time = now;
    
    auto [visible_start, visible_end] = GetVisibleRange();
    
    // 如果可见区域没有变化，则跳过
    // Skip if visible range hasn't changed
    if (last_loaded_range.first == visible_start && last_loaded_range.second == visible_end) {
        return;
    }
    last_loaded_range = {visible_start, visible_end};
    
    // 图标加载策略：优先加载首屏4个应用，然后加载即将进入屏幕的应用
    // Icon loading strategy: prioritize first screen 4 apps, then load upcoming screen apps
    constexpr size_t PRELOAD_BUFFER = 2; // 即将进入屏幕的应用数量
    constexpr size_t FIRST_SCREEN_SIZE = 4; // 首屏应用数量
    
    size_t load_start = (visible_start >= PRELOAD_BUFFER) ? (visible_start - PRELOAD_BUFFER) : 0;
    size_t load_end;
    {
        std::scoped_lock lock{entries_mutex};
        load_end = std::min(visible_end + PRELOAD_BUFFER, entries.size());
    }
    
    // 批量收集需要加载图标的应用信息，减少锁的使用
    // Batch collect applications that need icon loading to reduce lock usage
    struct LoadInfo {
        u64 application_id;
        int priority;
    };
    
    std::vector<LoadInfo> load_infos;
    load_infos.reserve(load_end - load_start);
    
    {
        std::scoped_lock lock{entries_mutex};
        // 确保索引范围有效
        // Ensure index range is valid
        const size_t actual_end = std::min(load_end, entries.size());
        
        for (size_t i = load_start; i < actual_end; ++i) {
            const auto& entry = entries[i];
            
            // 优化：同时检查图标状态和损坏状态，减少后续处理
            // Optimization: check both icon status and corruption status to reduce subsequent processing
            if (entry.image == this->default_icon_image && entry.name != corrupted_install) {
                LoadInfo info;
                info.application_id = entry.id;
                
                // 优先级策略：首屏4个应用最高优先级(0)，当前可见区域次高优先级(1)，其他为低优先级(2)
                // Priority strategy: first screen 4 apps highest priority(0), current visible area medium priority(1), others low priority(2)
                if (i < FIRST_SCREEN_SIZE) {
                    info.priority = 0; // 首屏应用最高优先级
                } else if (i >= visible_start && i < visible_end) {
                    info.priority = 1; // 当前可见区域次高优先级
                } else {
                    info.priority = 2; // 其他应用低优先级
                }
                
                load_infos.push_back(std::move(info));
            }
        }
    }
    
    // 如果没有需要加载的图标，直接返回
    // Return early if no icons need loading
    if (load_infos.empty()) {
        return;
    }
    
    // 处理收集到的加载信息
    // Process collected loading information
    for (const auto& info : load_infos) {
        
        // 提交图标加载任务
        // Submit icon loading task
        ResourceLoadTask icon_task;
        icon_task.application_id = info.application_id;
        icon_task.priority = info.priority;
        icon_task.submit_time = std::chrono::steady_clock::now();
        icon_task.task_type = ResourceTaskType::ICON; // 标记为图标任务 (Mark as icon task)
        
        icon_task.load_callback = [this, application_id = info.application_id]() {
            // 获取图标数据
            // Get icon data
            std::vector<unsigned char> icon_data;
            bool has_icon_data = false;
            
            // 优先使用AppEntry中缓存的图标数据，避免重复的缓存读取
            // Prioritize using cached icon data in AppEntry to avoid repeated cache reads
            {
                std::scoped_lock lock{entries_mutex};
                auto it = std::find_if(entries.begin(), entries.end(),
                    [application_id](const AppEntry& entry) {
                        return entry.id == application_id && entry.has_cached_icon;
                    });
                
                if (it != entries.end()) {
                    icon_data = it->cached_icon_data;
                    has_icon_data = true;
                }
            }
            
            
             if (!has_icon_data) {
                 return; // 没有可用的图标数据，直接返回
             }
            
            // 验证并创建图像
            // Validate and create image
            if (has_icon_data && !icon_data.empty() && IsValidJpegData(icon_data)) {
                int image_id = nvgCreateImageMem(this->vg, 0, icon_data.data(), icon_data.size());
                if (image_id > 0) {
                    std::scoped_lock lock{entries_mutex};
                    auto it = std::find_if(entries.begin(), entries.end(),
                        [application_id](const AppEntry& entry) {
                            return entry.id == application_id;
                        });
                    
                    if (it != entries.end()) {
                        // 如果之前有自己的图像，先删除
                        // If previously had own image, delete it first
                        if (it->own_image && it->image != this->default_icon_image) {
                            nvgDeleteImage(this->vg, it->image);
                        }
                        it->image = image_id;
                        it->own_image = true;
                    }
                }
            }
        };
        
        this->resource_manager.submitLoadTask(icon_task);
    }
}

// 卸载界面的可见区域图标加载 (Visible area icon loading for uninstall interface)
// 只加载当前屏幕可见的应用图标和下方2个预加载图标，避免内存浪费
// Only load icons for currently visible apps and 2 preload icons below, avoiding memory waste
void App::LoadConfirmVisibleAreaIcons() {
    // 如果没有选中的应用，直接返回 (Return early if no selected apps)
    if (this->selected_indices.empty()) {
        return;
    }
    
    // 获取当前可见范围 (Get current visible range)
    const auto [visible_start, visible_end] = GetConfirmVisibleRange();
    
    // 防抖机制：避免频繁加载 (Debouncing mechanism: avoid frequent loading)
    const auto current_time = std::chrono::steady_clock::now();
    const auto time_since_last_load = current_time - this->last_confirm_load_time;
    
    // 检查可见范围是否发生变化 (Check if visible range has changed)
    const std::pair<size_t, size_t> current_range = {visible_start, visible_end};
    const bool range_changed = (current_range != this->last_confirm_loaded_range);
    
    // 如果范围没有变化且距离上次加载时间不足防抖延迟，则跳过加载 (Skip loading if range unchanged and within debounce delay)
    if (!range_changed && time_since_last_load < LOAD_DEBOUNCE_MS) {
        return;
    }
    
    // 更新防抖状态 (Update debounce state)
    this->last_confirm_loaded_range = current_range;
    this->last_confirm_load_time = current_time;
    
    // 计算预加载范围：当前屏幕 + 下方2个应用 (Calculate preload range: current screen + 2 apps below)
    const size_t preload_buffer = 2;
    const size_t load_end = std::min(visible_end + preload_buffer, selected_indices.size());
    
    // 批量收集需要加载图标的应用信息 (Batch collect applications that need icon loading)
    struct LoadInfo {
        u64 application_id;
        int priority;
    };
    
    std::vector<LoadInfo> load_infos;
    load_infos.reserve(load_end - visible_start);
    
    {
        std::scoped_lock lock{entries_mutex};
        
        // 遍历可见范围和预加载范围内的应用 (Iterate through apps in visible and preload range)
        for (size_t i = visible_start; i < load_end; i++) {
            if (i >= selected_indices.size()) break;
            
            const size_t entry_index = selected_indices[i];
            if (entry_index >= entries.size()) continue;
            
            const auto& entry = entries[entry_index];
            
            // 如果图标未加载且应用未损坏，则添加到加载列表 (Add to load list if icon not loaded and app not corrupted)
            if (entry.image == this->default_icon_image && entry.name != corrupted_install) {
                LoadInfo info;
                info.application_id = entry.id;
                
                // 设置优先级：当前屏幕内的应用优先级最高，预加载区域优先级较低
                // Set priority: apps in current screen have highest priority, preload area has lower priority
                if (i < visible_end) {
                    info.priority = 1; // 可见区域高优先级 (High priority for visible area)
                } else {
                    info.priority = 2; // 预加载区域低优先级 (Low priority for preload area)
                }
                
                load_infos.push_back(std::move(info));
            }
        }
    }
    
    // 如果没有需要加载的图标，直接返回 (Return early if no icons need loading)
    if (load_infos.empty()) {
        return;
    }
    
    // 提交图标加载任务 (Submit icon loading tasks)
    for (const auto& info : load_infos) {
        ResourceLoadTask icon_task;
        icon_task.application_id = info.application_id;
        icon_task.priority = info.priority;
        icon_task.submit_time = std::chrono::steady_clock::now();
        icon_task.task_type = ResourceTaskType::ICON;
        
        icon_task.load_callback = [this, application_id = info.application_id]() {
            // 获取图标数据 (Get icon data)
            std::vector<unsigned char> icon_data;
            bool has_icon_data = false;
            
            // 使用AppEntry中缓存的图标数据 (Use cached icon data in AppEntry)
            {
                std::scoped_lock lock{entries_mutex};
                auto it = std::find_if(entries.begin(), entries.end(),
                    [application_id](const AppEntry& entry) {
                        return entry.id == application_id && entry.has_cached_icon;
                    });
                
                if (it != entries.end()) {
                    icon_data = it->cached_icon_data;
                    has_icon_data = true;
                }
            }
            
            if (!has_icon_data) {
                return; // 没有可用的图标数据，直接返回 (No available icon data, return early)
            }
            
            // 验证并创建图像 (Validate and create image)
            if (has_icon_data && !icon_data.empty() && IsValidJpegData(icon_data)) {
                int image_id = nvgCreateImageMem(this->vg, 0, icon_data.data(), static_cast<int>(icon_data.size()));
                if (image_id > 0) {
                    std::scoped_lock lock{entries_mutex};
                    auto it = std::find_if(entries.begin(), entries.end(),
                        [application_id](const AppEntry& entry) {
                            return entry.id == application_id;
                        });
                    
                    if (it != entries.end()) {
                        // 如果之前有自己的图像，先删除 (If previously had own image, delete it first)
                        if (it->own_image && it->image != this->default_icon_image) {
                            nvgDeleteImage(this->vg, it->image);
                        }
                        it->image = image_id;
                        it->own_image = true;
                    }
                }
            }
        };
        
        this->resource_manager.submitLoadTask(icon_task);
    }
}

// 加载卸载界面的图标 (Load icons for uninstall interface)
// 为卸载确认界面中的应用加载图标，确保独立于列表界面 (Load icons for apps in uninstall confirmation interface, independent of list interface)


App::App() {
    

    nsGetTotalSpaceSize(NcmStorageId_SdCard, (s64*)&this->sdcard_storage_size_total);
    nsGetFreeSpaceSize(NcmStorageId_SdCard, (s64*)&this->sdcard_storage_size_free);
    nsGetTotalSpaceSize(NcmStorageId_BuiltInUser, (s64*)&this->nand_storage_size_total);
    nsGetFreeSpaceSize(NcmStorageId_BuiltInUser, (s64*)&this->nand_storage_size_free);
    this->nand_storage_size_used = this->nand_storage_size_total - this->nand_storage_size_free;
    this->sdcard_storage_size_used = this->sdcard_storage_size_total - this->sdcard_storage_size_free;

    LOG("nand total: %lu free: %lu used: %lu\n", this->nand_storage_size_total, this->nand_storage_size_free, this->nand_storage_size_used);
    LOG("sdcard total: %lu free: %lu used: %lu\n", this->sdcard_storage_size_total, this->sdcard_storage_size_free, this->sdcard_storage_size_used);


    // 使用封装后的方法自动加载系统语言
    // Automatically load the system language using lang_manager
    tj::LangManager::getInstance().loadSystemLanguage();
    
    
    
    PlFontData font_standard, font_extended, font_lang;
    
    // 加载默认的字体，用于显示拉丁文 
    // Load the default font for displaying Latin characters
    plGetSharedFontByType(&font_standard, PlSharedFontType_Standard);
    plGetSharedFontByType(&font_extended, PlSharedFontType_NintendoExt);

    

    // Create the deko3d device
    this->device = dk::DeviceMaker{}.create();

    // Create the main queue
    this->queue = dk::QueueMaker{this->device}.setFlags(DkQueueFlags_Graphics).create();

    // Create the memory pools
    this->pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
    this->pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
    this->pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);

    // Create the static command buffer and feed it freshly allocated memory
    this->cmdbuf = dk::CmdBufMaker{this->device}.create();
    const CMemPool::Handle cmdmem = this->pool_data->allocate(this->StaticCmdSize);
    this->cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

    // Create the framebuffer resources
    this->createFramebufferResources();
    
    // 初始化动态命令缓冲区用于GPU命令优化
    // Initialize dynamic command buffers for GPU command optimization
    for (unsigned i = 0; i < NumCommandBuffers; ++i) {
        this->dynamic_cmdbufs[i] = dk::CmdBufMaker{this->device}.create();
        const CMemPool::Handle dynamic_cmdmem = this->pool_data->allocate(this->StaticCmdSize);
        this->dynamic_cmdbufs[i].addMemory(dynamic_cmdmem.getMemBlock(), dynamic_cmdmem.getOffset(), dynamic_cmdmem.getSize());
        
        // 初始化同步对象
        // Initialize synchronization objects
        this->command_fences[i] = {};
    }

    this->renderer.emplace(1280, 720, this->device, this->queue, *this->pool_images, *this->pool_code, *this->pool_data);
    this->vg = nvgCreateDk(&*this->renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);

    // 注册字体到NVG上下文
    // Register fonts to NVG context
    auto standard_font = nvgCreateFontMem(this->vg, "Standard", (unsigned char*)font_standard.address, font_standard.size, 0);
    auto extended_font = nvgCreateFontMem(this->vg, "Extended", (unsigned char*)font_extended.address, font_extended.size, 0);
    nvgAddFallbackFontId(this->vg, standard_font, extended_font);
    

    constexpr PlSharedFontType lang_font[] = {
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
        PlSharedFontType_KO,
    };


    for (auto type : lang_font) {
        if (R_SUCCEEDED(plGetSharedFontByType(&font_lang, type))) {
            char name[32];
            snprintf(name, sizeof(name), "Lang_%u", font_lang.type);
            auto lang_font = nvgCreateFontMem(this->vg, name, (unsigned char*)font_lang.address, font_lang.size, 0);
            nvgAddFallbackFontId(this->vg, standard_font, lang_font);
        } else {
            LOG("failed to load lang font %d\n", type);
        }
    }
    
    
    
    
    this->default_icon_image = nvgCreateImage(this->vg, "romfs:/default_icon.jpg", NVG_IMAGE_NEAREST);

    // 启动快速信息扫描
    // Start fast info scanning
    this->async_thread = util::async([this](std::stop_token stop_token){
            this->FastScanNames(stop_token);
            // 名称扫描完成后，触发初始视口加载
            // After name scanning is complete, trigger initial viewport loading
            if (!stop_token.stop_requested()) {
                this->LoadVisibleAreaIcons();
            }
        }
    );

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&this->pad);
}

/**
 * @brief App类的析构函数
 * 负责清理应用程序使用的所有资源，确保没有内存泄漏
 */
App::~App() {
    // 检查异步线程是否有效，如果有效则停止并等待其完成
    if (this->async_thread.valid()) {
        this->async_thread.request_stop(); // 请求线程停止
        this->async_thread.get(); // 等待线程完成
    }
    
    // 检查删除线程是否有效，如果有效则等待其完成
    if (this->delete_thread.valid()) {
        this->delete_thread.get(); // 等待删除线程完成
    }

    // 遍历所有应用条目，释放自行管理的图像资源
    for (auto&p : this->entries) {
        if (p.own_image) { // 仅释放由应用自己创建的图像
            nvgDeleteImage(this->vg, p.image); // 删除nanovg图像
        }
    }

    // 释放默认图标图像资源
    nvgDeleteImage(this->vg, default_icon_image);

    // GPU命令优化：等待所有命令缓冲区完成执行
    // GPU command optimization: Wait for all command buffers to complete execution
    for (unsigned i = 0; i < NumCommandBuffers; ++i) {
        this->waitForCommandCompletion(i);
    }
    
    // 销毁帧缓冲区相关资源
    this->destroyFramebufferResources();

    // 删除nanovg上下文
    nvgDeleteDk(this->vg);

    // 刷新缓存文件
    nxtcFlushCacheFile();

    // 退出libnxtc库
    nxtcExit();

    // 重置渲染器指针，释放相关资源
    this->renderer.reset();

    
}

void App::createFramebufferResources() {
    // Create layout for the depth buffer
    dk::ImageLayout layout_depthbuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(1280, 720)
        .initialize(layout_depthbuffer);

    // Create the depth buffer
    this->depthBuffer_mem = this->pool_images->allocate(layout_depthbuffer.getSize(), layout_depthbuffer.getAlignment());
    this->depthBuffer.initialize(layout_depthbuffer, this->depthBuffer_mem.getMemBlock(), this->depthBuffer_mem.getOffset());

    // Create layout for the framebuffers
    dk::ImageLayout layout_framebuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(1280, 720)
        .initialize(layout_framebuffer);

    // Create the framebuffers
    std::array<DkImage const*, NumFramebuffers> fb_array;
    const uint64_t fb_size  = layout_framebuffer.getSize();
    const uint32_t fb_align = layout_framebuffer.getAlignment();
    for (unsigned i = 0; i < fb_array.size(); i++) {
        // Allocate a framebuffer
        this->framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
        this->framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

        // Generate a command list that binds it
        dk::ImageView colorTarget{ framebuffers[i] }, depthTarget{ depthBuffer };
        this->cmdbuf.bindRenderTargets(&colorTarget, &depthTarget);
        this->framebuffer_cmdlists[i] = cmdbuf.finishList();

        // Fill in the array for use later by the swapchain creation code
        fb_array[i] = &framebuffers[i];
    }

    // Create the swapchain using the framebuffers
    // 启用垂直同步以避免画面撕裂
    NWindow* nwin = nwindowGetDefault();
    nwindowSetSwapInterval(nwin, 1); // 设置交换间隔为1，启用垂直同步
    this->swapchain = dk::SwapchainMaker{device, nwin, fb_array}.create();

    // Generate the main rendering cmdlist
    this->recordStaticCommands();
}

void App::destroyFramebufferResources() {
    // Return early if we have nothing to destroy
    if (!this->swapchain) {
        return;
    }

    this->queue.waitIdle();
    this->cmdbuf.clear();
    swapchain.destroy();

    // Destroy the framebuffers
    for (unsigned i = 0; i < NumFramebuffers; i++) {
        framebuffers_mem[i].destroy();
    }

    // Destroy the depth buffer
    this->depthBuffer_mem.destroy();
}

void App::recordStaticCommands() {
    // Initialize state structs with deko3d defaults
    dk::RasterizerState rasterizerState;
    dk::ColorState colorState;
    dk::ColorWriteState colorWriteState;
    dk::BlendState blendState;

    // Configure the viewport and scissor
    this->cmdbuf.setViewports(0, { { 0.0f, 0.0f, 1280, 720, 0.0f, 1.0f } });
    this->cmdbuf.setScissors(0, { { 0, 0, 1280, 720 } });

    // Clear the color and depth buffers
    this->cmdbuf.clearColor(0, DkColorMask_RGBA, 0.2f, 0.3f, 0.3f, 1.0f);
    this->cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

    // Bind required state
    this->cmdbuf.bindRasterizerState(rasterizerState);
    this->cmdbuf.bindColorState(colorState);
    this->cmdbuf.bindColorWriteState(colorWriteState);

    this->render_cmdlist = this->cmdbuf.finishList();
}

// GPU命令优化：准备下一个命令缓冲区
// GPU command optimization: Prepare next command buffer
void App::prepareNextCommandBuffer() {
    // 等待当前命令缓冲区完成（如果已提交）
    // Wait for current command buffer completion (if submitted)
    if (this->command_submitted[this->current_cmdbuf_index]) {
        this->waitForCommandCompletion(this->current_cmdbuf_index);
        this->command_submitted[this->current_cmdbuf_index] = false;
    }
    
    // 切换到下一个命令缓冲区
    // Switch to next command buffer
    this->current_cmdbuf_index = (this->current_cmdbuf_index + 1) % NumCommandBuffers;
}

// GPU命令优化：提交当前命令缓冲区
// GPU command optimization: Submit current command buffer
void App::submitCurrentCommandBuffer() {
    if (this->dynamic_cmdlists[this->current_cmdbuf_index]) {
        // 设置同步点
        // Set synchronization point
        this->dynamic_cmdbufs[this->current_cmdbuf_index].signalFence(this->command_fences[this->current_cmdbuf_index]);
        
        // 提交命令列表
        // Submit command list
        this->queue.submitCommands(this->dynamic_cmdlists[this->current_cmdbuf_index]);
        this->command_submitted[this->current_cmdbuf_index] = true;
    }
}

// GPU命令优化：等待指定命令缓冲区完成
// GPU command optimization: Wait for specified command buffer completion
void App::waitForCommandCompletion(unsigned buffer_index) {
    if (buffer_index < NumCommandBuffers && this->command_submitted[buffer_index]) {
        // 等待fence信号
        // Wait for fence signal
        this->command_fences[buffer_index].wait();
    }
}

} // namespace tj
