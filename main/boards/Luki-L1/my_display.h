#pragma once

#include <libs/gif/lv_gif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "display/lcd_display.h"
#include "application.h"  // 添加这个头文件以获取设备状态
 

// 显示更新消息类型
enum DisplayUpdateType {
    DISPLAY_UPDATE_EMOTION,
    DISPLAY_UPDATE_CHAT_MESSAGE,
    DISPLAY_UPDATE_ICON
};

// 显示更新消息结构
struct DisplayUpdateMessage {
    DisplayUpdateType type;
    union {
        struct {
            char emotion[32];
        } emotion_data;
        struct {
            char role[16];
            char content[256];
        } chat_data;
        struct {
            char icon[64];
        } icon_data;
    };
};

/**
 * @brief GIF表情显示类
 * 继承LcdDisplay，添加GIF表情支持
 */
class CustomEmojiDisplay : public SpiLcdDisplay {
public:
    /**
     * @brief 构造函数，参数与SpiLcdDisplay相同
     */
    CustomEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy, DisplayFonts fonts);

    virtual ~CustomEmojiDisplay();

    // 重写表情设置方法 - 异步版本
    virtual void SetEmotion(const char* emotion) override;

    // 重写聊天消息设置方法 - 异步版本
    virtual void SetChatMessage(const char* role, const char* content) override;

    // 添加SetIcon方法声明 - 异步版本
    virtual void SetIcon(const char* icon) override;

private:
    void SetupGifContainer();
    
    // 同步版本的显示更新方法（在显示线程中调用）
    void SetEmotionSync(const char* emotion);
    void SetChatMessageSync(const char* role, const char* content);
    void SetIconSync(const char* icon);
    
    // 解析OTA进度消息并在状态栏显示
    bool ParseAndDisplayOTAProgress(const char* content);
    
    // 显示刷新线程相关
    static void DisplayRefreshTask(void* pvParameters);
    void ProcessDisplayUpdates();
    
    // 启动显示刷新线程
    bool StartDisplayRefreshThread();
    
    // 停止显示刷新线程
    void StopDisplayRefreshThread();

    lv_obj_t* emotion_gif_;  ///< GIF表情组件
    
    // 显示刷新线程相关成员
    TaskHandle_t display_task_handle_;
    QueueHandle_t display_queue_;
    bool display_thread_running_;

    // 表情映射
    struct EmotionMap {
        const char* name;
        const lv_img_dsc_t* gif;
    };

    static const EmotionMap emotion_maps_[];
};