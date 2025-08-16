#pragma once

#include "nanovg/nanovg.h"
#include "nanovg/deko3d/dk_renderer.hpp"
#include "async.hpp"
#include "audio_manager.hpp"

#include <switch.h>
#include <cstdint>
#include <vector>
#include <string>
#include <future>
#include <mutex>
#include <optional>
#include <functional>
#include <stop_token>
#include <utility>
#include <queue>
#include <chrono>

namespace tj {

using AppID = std::uint64_t;

enum class MenuMode { LOAD, LIST, CONFIRM };

struct Controller final {
    // these are tap only
    bool A;
    bool B;
    bool X;
    bool Y;
    bool L;
    bool R;
    bool L2;
    bool R2;
    bool START;
    bool SELECT;
    // these can be held
    bool LEFT;
    bool RIGHT;
    bool UP;
    bool DOWN;
    // RIGHT+A组合键相关变量 (RIGHT+A combination key related variables)
    // 简单检测：只要两个键都按下就触发，避免重复触发 (Simple detection: trigger when both keys are pressed, avoid repeated triggering)
    bool RIGHT_AND_A = false; // RIGHT+A组合键状态 (RIGHT+A combination key state)

    static constexpr int MAX = 1000;
    static constexpr int MAX_STEP = 250;
    int step = 50;
    int counter = 0;

    void UpdateButtonHeld(bool& down, bool held);
};

struct AppEntry final {
    std::string name;
    std::string author;
    std::string display_version;
    std::size_t size_nand;
    std::size_t size_sd;
    std::size_t size_total;
    AppID id;
    int image;
    bool selected{false};
    bool own_image{false};
    
    // 缓存的原始图标数据，避免重复从缓存读取
    // Cached raw icon data to avoid repeated cache reads
    std::vector<unsigned char> cached_icon_data;
    bool has_cached_icon{false};
};

struct NsDeleteData final {
    std::vector<AppID> entries;
    std::function<void(bool)> del_cb; // called when deleted an entry
    std::function<void(void)> done_cb; // called when finished
};

// 资源加载任务结构体
// Resource loading task structure
enum class ResourceTaskType {
    ICON     // 图标加载任务 (Icon loading task)
};

struct ResourceLoadTask {
    u64 application_id;
    std::function<void()> load_callback;
    std::chrono::steady_clock::time_point submit_time;
    int priority; // 优先级，数值越小优先级越高 (Priority, lower value = higher priority)
    ResourceTaskType task_type; // 任务类型 (Task type)
};

// 资源加载管理器
// Resource loading manager
class ResourceLoadManager {
private:
    // 优先级队列比较器：优先级数值越小，优先级越高
    // Priority queue comparator: lower priority value = higher priority
    struct TaskComparator {
        bool operator()(const ResourceLoadTask& a, const ResourceLoadTask& b) const {
            if (a.priority != b.priority) {
                return a.priority > b.priority; // 优先级高的在前 (Higher priority first)
            }
            return a.submit_time > b.submit_time; // 相同优先级按提交时间排序 (Same priority sorted by submit time)
        }
    };
    
    std::priority_queue<ResourceLoadTask, std::vector<ResourceLoadTask>, TaskComparator> pending_tasks;
    mutable std::mutex task_mutex;
    static constexpr int MAX_ICON_LOADS_PER_FRAME = 2;  // 每帧最大图标加载数量 (Max icon loads per frame)
    
public:
    void submitLoadTask(const ResourceLoadTask& task);
    void processFrameLoads(); // 在每帧调用 (Called every frame)
    bool hasPendingTasks() const;
    size_t getPendingTaskCount() const;
};

class App final {
public:
    App();
    ~App();
    void Loop();

private:


    // 快速获取应用基本信息并缓存图标数据
    // Fast get application basic info and cache icon data
    bool TryGetAppBasicInfoWithIconCache(u64 application_id, AppEntry& entry);
    
    // 获取应用大小信息
    // Get application size information
    void GetAppSizeInfo(u64 application_id, AppEntry& entry);
    
    // 分离式扫描：第一阶段快速扫描应用名称
    // Separated scanning: Phase 1 - Fast scan application names
    void FastScanNames(std::stop_token stop_token);
    

    
    // 基于视口的智能图标加载：根据光标位置优先加载可见区域的图标
    // Viewport-aware smart icon loading: prioritize loading icons in visible area based on cursor position
    void LoadVisibleAreaIcons();

    void LoadConfirmVisibleAreaIcons(); // 卸载界面的可见区域图标加载 (Visible area icon loading for uninstall interface)
    std::pair<size_t, size_t> GetConfirmVisibleRange() const; // 获取卸载界面可见范围 (Get visible range for confirm interface)
    
    // 计算当前可见区域的应用索引范围
    // Calculate the index range of applications in current visible area
    std::pair<size_t, size_t> GetVisibleRange() const;
    
    // 上次加载的可见区域范围，用于防抖
    // Last loaded visible range for debouncing
    mutable std::pair<size_t, size_t> last_loaded_range{SIZE_MAX, SIZE_MAX};
    
    // 上次调用LoadVisibleAreaIcons的时间，用于防抖
    // Last time LoadVisibleAreaIcons was called, for debouncing
    mutable std::chrono::steady_clock::time_point last_load_time{};
    
    // 卸载界面视口感知图标加载的防抖和缓存机制 (Debouncing and caching mechanism for confirm interface viewport-aware icon loading)
    mutable std::pair<size_t, size_t> last_confirm_loaded_range{SIZE_MAX, SIZE_MAX}; // 上次卸载界面加载的范围 (Last loaded range for confirm interface)
    mutable std::chrono::steady_clock::time_point last_confirm_load_time{}; // 上次卸载界面加载时间 (Last load time for confirm interface)
    static constexpr auto LOAD_DEBOUNCE_MS = std::chrono::milliseconds(100); // 防抖延迟100ms (100ms debounce delay)

    NVGcontext* vg{nullptr};
    std::vector<AppEntry> entries;
    std::vector<AppID> delete_entries;
    PadState pad{};
    Controller controller{};
    int default_icon_image{};

    std::size_t nand_storage_size_total{};
    std::size_t nand_storage_size_used{};
    std::size_t nand_storage_size_free{};
    std::size_t sdcard_storage_size_total{};
    std::size_t sdcard_storage_size_used{};
    std::size_t sdcard_storage_size_free{};

    util::AsyncFurture<void> async_thread;
    util::AsyncFurture<void> delete_thread; // 专门用于删除操作的线程 (Thread specifically for deletion operations)
    std::mutex mutex{};
    static std::mutex entries_mutex;
    std::size_t delete_index{}; // mutex locked
    bool finished_scanning{false}; // mutex locked
    bool finished_deleting{false}; // mutex locked
    bool deletion_interrupted{false}; // 标记是否发生过删除中断 (Flag to mark if deletion was interrupted)
    double deleted_nand_bytes{0.0}; // 已删除应用在NAND上的总字节数 (Total bytes of deleted applications on NAND)
    double deleted_sd_bytes{0.0}; // 已删除应用在SD卡上的总字节数 (Total bytes of deleted applications on SD card)
    std::size_t deleted_app_count{0}; // 已删除应用的数量 (Number of deleted applications)
    std::size_t selected_nand_total_bytes{0}; // 已选中应用在NAND上的总容量 (Total capacity of selected applications on NAND)
    std::size_t selected_sd_total_bytes{0}; // 已选中应用在SD卡上的总容量 (Total capacity of selected applications on SD card)
    static std::atomic<bool> initial_batch_loaded;
    static std::atomic<size_t> scanned_count;
    static std::atomic<size_t> total_count;
    static std::atomic<bool> is_scan_running;

    // 资源加载管理器
    // Resource loading manager
    ResourceLoadManager resource_manager;
    
    // 每帧资源加载控制
    // Per-frame resource loading control
    std::chrono::steady_clock::time_point last_frame_time;
    bool enable_frame_load_limit{true}; // 是否启用每帧加载限制 (Whether to enable per-frame load limit)

    // this is just bad code, ignore it
    static constexpr float BOX_HEIGHT{120.f};
    float yoff{130.f};
    float ypos{130.f};
    std::size_t confirm_start{0};
    std::size_t start{0};
    std::size_t delete_count{0};
    std::size_t index{}; // where i am in the array
    std::size_t confirm_index{}; // 确认界面中选中的索引 (Selected index in confirm menu)
    std::vector<std::size_t> selected_indices; // 确认界面中已选中应用的索引列表 (List of indices of selected applications in confirm menu)
    MenuMode menu_mode{MenuMode::LOAD};

    bool quit{false};

    enum class SortType {
        Size_BigSmall,
        Alphabetical,
        MAX,
    };

    uint8_t sort_type{std::to_underlying(SortType::Size_BigSmall)};
    float FPS{0.0f}; // 当前帧率 (Current frame rate)
    
    AudioManager audio_manager; // 音效管理器 (Audio manager)

    void Draw();
    void Update();
    void Poll();

    void Sort();
    const char* GetSortStr();

    void UpdateLoad();
    void UpdateList();
    void UpdateConfirm();


    void DrawBackground();
    void DrawLoad();
    void DrawList();
    void DrawConfirm();

    Result GetAllApplicationIds(std::vector<u64>& app_ids);

private: // from nanovg decko3d example by adubbz
    static constexpr unsigned NumFramebuffers = 2;
    static constexpr unsigned StaticCmdSize = 0x1000;
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;
    dk::UniqueCmdBuf cmdbuf;
    CMemPool::Handle depthBuffer_mem;
    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image depthBuffer;
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;
    DkCmdList render_cmdlist;
    std::optional<nvg::DkRenderer> renderer;
    
    // GPU命令优化：双缓冲命令列表用于分离提交和执行
    // GPU command optimization: Double-buffered command lists for separating submission and execution
    static constexpr unsigned NumCommandBuffers = 2;
    dk::UniqueCmdBuf dynamic_cmdbufs[NumCommandBuffers];
    DkCmdList dynamic_cmdlists[NumCommandBuffers];
    unsigned current_cmdbuf_index{0};
    bool command_submitted[NumCommandBuffers]{false, false};
    
    // GPU同步对象用于跟踪命令执行状态
    // GPU synchronization objects for tracking command execution status
    dk::Fence command_fences[NumCommandBuffers];
    void createFramebufferResources();
    void destroyFramebufferResources();
    void recordStaticCommands();
    
    // GPU命令优化相关函数
    // GPU command optimization related functions
    void prepareNextCommandBuffer();
    void submitCurrentCommandBuffer();
    void waitForCommandCompletion(unsigned buffer_index);
};

} // namespace tj
