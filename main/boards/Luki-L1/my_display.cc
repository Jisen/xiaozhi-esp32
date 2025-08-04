#include "my_display.h"

#include <esp_log.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "display/lcd_display.h"
#include "font_awesome_symbols.h"

LV_IMG_DECLARE(neutral);
LV_IMG_DECLARE(happy);
// LV_IMG_DECLARE(laughing);
// LV_IMG_DECLARE(funny);
LV_IMG_DECLARE(sad);
LV_IMG_DECLARE(angry);
// LV_IMG_DECLARE(crying);
// LV_IMG_DECLARE(loving);
// LV_IMG_DECLARE(embarrassed);
LV_IMG_DECLARE(surprised);
// LV_IMG_DECLARE(shocked);
LV_IMG_DECLARE(thinking);
LV_IMG_DECLARE(winking);
// LV_IMG_DECLARE(cool);
// LV_IMG_DECLARE(relaxed);
// LV_IMG_DECLARE(delicious);
LV_IMG_DECLARE(kissy);
LV_IMG_DECLARE(confident);
LV_IMG_DECLARE(sleepy);
LV_IMG_DECLARE(silly);
LV_IMG_DECLARE(confused);


#define TAG "CustomEmojiDisplay"
#define DISPLAY_QUEUE_SIZE 10
#define DISPLAY_TASK_STACK_SIZE 4096
#define DISPLAY_TASK_PRIORITY 0  // 更低的优先级，确保不影响网络通信
#define DISPLAY_REFRESH_DELAY_MS 5  // 增加显示刷新间隔，减少CPU占用

// 表情映射表 - 将原版21种表情映射到现有6个GIF
const CustomEmojiDisplay::EmotionMap CustomEmojiDisplay::emotion_maps_[] = {
    // 中性/平静类表情 -> neutral
    { "neutral",&neutral},
    { "happy",&happy},
    // { "laughing",&laughing},
    // { "funny",&funny},
    { "sad",&sad},
    { "angry",&angry},
    // {"crying", &crying},
    // {"loving", &loving},
    // {"embarrassed", &embarrassed},
    { "surprised",&surprised},
    // {"shocked",&shocked},
    {"thinking",&thinking},
    {"winking",&winking},
    // {"cool",&cool},
    // {"relaxed", &relaxed},
    // {"delicious", &delicious},
    {"kissy", &kissy},
    {"confident",&confident},
    {"sleepy",&sleepy},
    {"silly", &silly},
    {"confused", &confused},

    {nullptr, nullptr}  // 结束标记
};



CustomEmojiDisplay::CustomEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                   int width, int height, int offset_x, int offset_y, bool mirror_x,
                                   bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy,
                    fonts),
      emotion_gif_(nullptr),
      display_task_handle_(nullptr),
      display_queue_(nullptr),
      display_thread_running_(false) {
    SetupGifContainer();
    
    // 启动显示刷新线程
    if (!StartDisplayRefreshThread()) {
        ESP_LOGE(TAG, "Failed to start display refresh thread");
    }
};

CustomEmojiDisplay::~CustomEmojiDisplay() {
    StopDisplayRefreshThread();
}

bool CustomEmojiDisplay::StartDisplayRefreshThread() {
    // 创建消息队列
    display_queue_ = xQueueCreate(DISPLAY_QUEUE_SIZE, sizeof(DisplayUpdateMessage));
    if (display_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create display queue");
        return false;
    }
    
    display_thread_running_ = true;
    
    // 创建显示刷新任务，使用最低优先级
    BaseType_t result = xTaskCreatePinnedToCore(
        DisplayRefreshTask,
        "display_refresh",
        DISPLAY_TASK_STACK_SIZE,
        this,
        DISPLAY_TASK_PRIORITY,
        &display_task_handle_,
        1  // 固定到核心1，避免与主任务冲突
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display refresh task");
        display_thread_running_ = false;
        vQueueDelete(display_queue_);
        display_queue_ = nullptr;
        return false;
    }
    
    ESP_LOGI(TAG, "Display refresh thread started with priority %d", DISPLAY_TASK_PRIORITY);
    return true;
}

void CustomEmojiDisplay::StopDisplayRefreshThread() {
    if (display_thread_running_) {
        display_thread_running_ = false;
        
        // 发送一个空消息来唤醒线程
        DisplayUpdateMessage stop_msg = {};
        xQueueSend(display_queue_, &stop_msg, 0);
        
        // 等待任务结束
        if (display_task_handle_ != nullptr) {
            vTaskDelete(display_task_handle_);
            display_task_handle_ = nullptr;
        }
        
        // 删除队列
        if (display_queue_ != nullptr) {
            vQueueDelete(display_queue_);
            display_queue_ = nullptr;
        }
        
        ESP_LOGI(TAG, "Display refresh thread stopped");
    }
}

void CustomEmojiDisplay::DisplayRefreshTask(void* pvParameters) {
    CustomEmojiDisplay* display = static_cast<CustomEmojiDisplay*>(pvParameters);
    display->ProcessDisplayUpdates();
}

void CustomEmojiDisplay::ProcessDisplayUpdates() {
    DisplayUpdateMessage msg;
    
    ESP_LOGI(TAG, "Display refresh task started with priority %d", uxTaskPriorityGet(NULL));
    
    while (display_thread_running_) {
        // 等待显示更新消息，使用较长的超时时间以节省CPU
        if (xQueueReceive(display_queue_, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (!display_thread_running_) {
                break;
            }
            
            // 处理不同类型的显示更新
            switch (msg.type) {
                case DISPLAY_UPDATE_EMOTION:
                    SetEmotionSync(msg.emotion_data.emotion);
                    break;
                    
                case DISPLAY_UPDATE_CHAT_MESSAGE:
                    SetChatMessageSync(msg.chat_data.role, msg.chat_data.content);
                    break;
                    
                case DISPLAY_UPDATE_ICON:
                    SetIconSync(msg.icon_data.icon);
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown display update type: %d", msg.type);
                    break;
            }
            
            // 增加延迟，减少CPU占用，让出更多时间给网络通信
            vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_DELAY_MS));
        }
    }
    
    ESP_LOGI(TAG, "Display refresh task ended");
}

// 异步版本的SetEmotion
void CustomEmojiDisplay::SetEmotion(const char* emotion) {
    if (!emotion || !display_queue_) {
        return;
    }
    
    DisplayUpdateMessage msg;
    msg.type = DISPLAY_UPDATE_EMOTION;
    strncpy(msg.emotion_data.emotion, emotion, sizeof(msg.emotion_data.emotion) - 1);
    msg.emotion_data.emotion[sizeof(msg.emotion_data.emotion) - 1] = '\0';
    
    // 非阻塞发送，如果队列满了就丢弃旧消息
    if (xQueueSend(display_queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Display queue full, dropping emotion update");
    }
}

// 异步版本的SetChatMessage
void CustomEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    if (!display_queue_) {
        return;
    }
    
    DisplayUpdateMessage msg;
    msg.type = DISPLAY_UPDATE_CHAT_MESSAGE;
    
    if (role) {
        strncpy(msg.chat_data.role, role, sizeof(msg.chat_data.role) - 1);
        msg.chat_data.role[sizeof(msg.chat_data.role) - 1] = '\0';
    } else {
        msg.chat_data.role[0] = '\0';
    }
    
    if (content) {
        strncpy(msg.chat_data.content, content, sizeof(msg.chat_data.content) - 1);
        msg.chat_data.content[sizeof(msg.chat_data.content) - 1] = '\0';
    } else {
        msg.chat_data.content[0] = '\0';
    }
    
    // 非阻塞发送
    if (xQueueSend(display_queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Display queue full, dropping chat message update");
    }
}

// 异步版本的SetIcon
void CustomEmojiDisplay::SetIcon(const char* icon) {
    if (!icon || !display_queue_) {
        return;
    }
    
    DisplayUpdateMessage msg;
    msg.type = DISPLAY_UPDATE_ICON;
    strncpy(msg.icon_data.icon, icon, sizeof(msg.icon_data.icon) - 1);
    msg.icon_data.icon[sizeof(msg.icon_data.icon) - 1] = '\0';
    
    // 非阻塞发送
    if (xQueueSend(display_queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Display queue full, dropping icon update");
    }
}

void CustomEmojiDisplay::SetupGifContainer() {
    DisplayLockGuard lock(this);

    if (emotion_label_) {
        lv_obj_del(emotion_label_);
    }

    if (chat_message_label_) {
        lv_obj_del(chat_message_label_);
    }
    if (content_) {
        lv_obj_del(content_);
    }

    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);

    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(content_, LV_HOR_RES, LV_HOR_RES);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_center(content_);

    emotion_label_ = lv_label_create(content_);
    lv_label_set_text(emotion_label_, "");
    lv_obj_set_width(emotion_label_, 0);
    lv_obj_set_style_border_width(emotion_label_, 0, 0);
    lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);

    emotion_gif_ = lv_gif_create(content_);
    // int gif_size = LV_HOR_RES;
    // lv_obj_set_size(emotion_gif_, gif_size, gif_size);
    // lv_obj_set_style_border_width(emotion_gif_, 0, 0);
    // lv_obj_set_style_bg_opa(emotion_gif_, LV_OPA_TRANSP, 0);
    lv_obj_center(emotion_gif_);
    lv_gif_set_src(emotion_gif_, &neutral);

    //新添加循环
    lv_gif_set_loop_count(emotion_gif_,0xffffffff);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
    lv_obj_set_style_border_width(chat_message_label_, 0, 0);
    lv_obj_set_style_text_opa(chat_message_label_, LV_OPA_TRANSP,0);//字符透明

    // lv_obj_set_style_bg_opa(chat_message_label_, LV_OPA_70, 0);
    // lv_obj_set_style_bg_color(chat_message_label_, lv_color_black(), 0);
    // lv_obj_set_style_pad_ver(chat_message_label_, 5, 0);

    lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, 0);

    LcdDisplay::SetTheme("dark");
}

// 同步版本的SetEmotion（在显示线程中调用）
void CustomEmojiDisplay::SetEmotionSync(const char* emotion) {
    if (!emotion || !emotion_gif_) {
        return;
    }

    DisplayLockGuard lock(this);

    for (const auto& map : emotion_maps_) {
        if (map.name && strcmp(map.name, emotion) == 0) {
            lv_gif_set_src(emotion_gif_, map.gif);
            ESP_LOGI(TAG, "设置表情: %s", emotion);
            
            // // 根据设备状态和表情来决定是否显示状态栏
            // if (status_bar_ != nullptr) {
            //     auto device_state = Application::GetInstance().GetDeviceState();
                
            //     // 在聆听状态下显示状态栏，即使是neutral表情
            //     if (device_state == kDeviceStateListening) {
            //         lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            //     } else {
            //         // 在其他状态下，只有neutral表情才隐藏状态栏
            //         if (strcmp(emotion, "neutral") == 0) {
            //             lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            //         } else {
            //             lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            //         }
            //     }
            // }
            
            return;
        }
    }

    // 默认使用neutral表情
    lv_gif_set_src(emotion_gif_, &neutral);
    ESP_LOGI(TAG, "未知表情'%s'，使用默认", emotion);
    
    // // 默认情况下根据设备状态决定状态栏显示
    // if (status_bar_ != nullptr) {
    //     auto device_state = Application::GetInstance().GetDeviceState();
        
    //     // 在聆听状态下显示状态栏
    //     if (device_state == kDeviceStateListening) {
    //         lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    //     } else {
    //         // 在其他状态下隐藏状态栏（因为使用neutral）
    //         lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    //     }
    // }
}

// 同步版本的SetChatMessage（在显示线程中调用）
void CustomEmojiDisplay::SetChatMessageSync(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    if (content == nullptr || strlen(content) == 0) {
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(chat_message_label_, content);
    lv_obj_clear_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "设置聊天消息 [%s]: %s", role, content);
}

// 同步版本的SetIcon（在显示线程中调用）
void CustomEmojiDisplay::SetIconSync(const char* icon) {
    if (!icon) {
        return;
    }

    DisplayLockGuard lock(this);

    if (chat_message_label_ != nullptr) {
        std::string icon_message = std::string(icon) + " ";

        if (strcmp(icon, FONT_AWESOME_DOWNLOAD) == 0) {
            icon_message += "正在升级...";
        } else {
            icon_message += "系统状态";
        }

        lv_label_set_text(chat_message_label_, icon_message.c_str());
        lv_obj_clear_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

        ESP_LOGI(TAG, "设置图标: %s", icon);
    }
}
