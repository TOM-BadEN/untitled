/**
 * @file audio_manager.hpp
 * @brief 音效管理器类，使用switch-libpulsar播放Switch系统自带音效
 */
#pragma once

#include <switch.h>
#include <pulsar.h>
#include <chrono>

/**
 * @brief 音效管理器类
 * 负责初始化音频系统、加载系统音效并提供播放接口
 */
class AudioManager {
public:
    /**
     * @brief 构造函数
     */
    AudioManager();
    
    /**
     * @brief 析构函数
     */
    ~AudioManager();
    
    /**
     * @brief 初始化音效系统
     * @return 成功返回true，失败返回false
     */
    bool Initialize();
    
    /**
     * @brief 清理音效系统
     */
    void Cleanup();
    
    /**
     * @brief 播放按键音效
     * @param volume 音量 (0.0f - 1.0f)
     */
    void PlayKeySound(float volume = 1.0f);
    
    /**
     * @brief 播放确认音效
     * @param volume 音量 (0.0f - 1.0f)
     */
    void PlayConfirmSound(float volume = 1.0f);
    
    /**
     * @brief 播放取消音效
     * @param volume 音量 (0.0f - 1.0f)
     */
    void PlayCancelSound(float volume = 1.0f);
    
    /**
     * @brief 播放滚动音效
     * @param volume 音量 (0.0f - 1.0f)
     */
    // void PlayScrollSound(float volume = 1.0f);
    
    /**
     * @brief 播放限制/边界音效
     * @param volume 音量 (0.0f - 1.0f)
     */
    void PlayLimitSound(float volume = 1.0f);
    
    /**
     * @brief 播放启动音效
     * @param volume 音量 (0.0f - 1.0f)
     */
    // void PlayStartupSound(float volume = 1.0f);
    
    /**
     * @brief 检查音效系统是否已初始化
     * @return 已初始化返回true，否则返回false
     */
    bool IsInitialized() const { return m_initialized; }
    
private:
    bool m_initialized;                    ///< 初始化状态
    PLSR_BFSAR m_bfsar;                   ///< BFSAR音频档案
    PLSR_PlayerSoundId m_keySoundId;      ///< 按键音效ID (SeGameIconFocus)
    PLSR_PlayerSoundId m_confirmSoundId;  ///< 确认音效ID (SeGameIconAdd)
    PLSR_PlayerSoundId m_cancelSoundId;   ///< 取消音效ID (SeInsertError)
    // PLSR_PlayerSoundId m_scrollSoundId;   ///< 滚动音效ID (SeGameIconScroll)
    PLSR_PlayerSoundId m_limitSoundId;    ///< 限制/边界音效ID (SeGameIconLimit)
    // PLSR_PlayerSoundId m_startupSoundId;  ///< 启动音效ID (StartupMenu_Game)
    
    // 防抖机制相关变量 (Debounce mechanism related variables)
    static constexpr auto AUDIO_DEBOUNCE_MS = std::chrono::milliseconds(120); ///< 音效防抖间隔120ms
    std::chrono::steady_clock::time_point m_lastKeySoundTime{};     ///< 上次按键音效播放时间
    std::chrono::steady_clock::time_point m_lastConfirmSoundTime{}; ///< 上次确认音效播放时间
    std::chrono::steady_clock::time_point m_lastCancelSoundTime{};  ///< 上次取消音效播放时间
    std::chrono::steady_clock::time_point m_lastLimitSoundTime{};   ///< 上次限制音效播放时间
    
    /**
     * @brief 加载系统音效
     * @return 成功返回true，失败返回false
     */
    bool LoadSystemSounds();
};