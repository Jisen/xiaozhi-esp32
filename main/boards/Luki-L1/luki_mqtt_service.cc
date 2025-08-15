#include "luki_mqtt_service.h"
#include "luki_mqtt_config.h"
#include "board.h"
#include "system_info.h"

#include <mqtt.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <string.h>
// #include <algorithm>
// #include <time.h>
// #include <sys/time.h>

static const char* TAG = "LukiMqtt";

LukiMqttService::LukiMqttService() {
}

LukiMqttService::~LukiMqttService() {
    ESP_LOGI(TAG, "Destroying MQTT service...");
    
    // 如果还有任务和队列没有被Stop()清理，现在清理
    if (task_handle_ || publish_queue_) {
        ESP_LOGW(TAG, "Stop() was not called, cleaning up now...");
        
        // 先标记队列为无效，防止任务继续使用
        QueueHandle_t temp_queue = publish_queue_;
        publish_queue_ = nullptr;
        
        // 删除任务
        if (task_handle_) {
            vTaskDelete(task_handle_);
            task_handle_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(100));  // 等待任务完全停止
        }
        
        // 删除队列
        if (temp_queue) {
            vQueueDelete(temp_queue);
        }
    }
    
    luki_mqtt_.reset();
    ESP_LOGI(TAG, "MQTT service destroyed");
}

void LukiMqttService::Start() {
    // 使用编译时配置常量
    endpoint_ = LUKI_MQTT_ENDPOINT;
    // 生成唯一的Client ID: 前缀 + 完整MAC地址(去除冒号) + 启动时间戳
    std::string mac_clean = SystemInfo::GetMacAddress();
    // 移除MAC地址中的冒号
    mac_clean.erase(std::remove(mac_clean.begin(), mac_clean.end(), ':'), mac_clean.end());
    
    // 获取启动时间戳作为唯一标识
    uint32_t boot_time = esp_timer_get_time() / 1000000;  // 转换为秒
    
    client_id_ = std::string(LUKI_MQTT_CLIENT_ID_PREFIX) + mac_clean + "_" + std::to_string(boot_time);
    username_ = LUKI_MQTT_USERNAME;
    password_ = LUKI_MQTT_PASSWORD;
    int keepalive = LUKI_MQTT_KEEPALIVE;

    if (endpoint_.empty()) {
        ESP_LOGW(TAG, "MQTT endpoint is empty, service not started");
        return;
    }

#if LUKI_MQTT_HIDE_CREDENTIALS
    ESP_LOGI(TAG, "MQTT config: endpoint=%s, client_id=%s", endpoint_.c_str(), client_id_.c_str());
#else
    ESP_LOGI(TAG, "MQTT config: endpoint=%s, client_id=%s, username=%s", 
             endpoint_.c_str(), client_id_.c_str(), username_.c_str());
#endif

    auto network = Board::GetInstance().GetNetwork();
    if (!network) {
        ESP_LOGE(TAG, "Network interface not available");
        return;
    }

    luki_mqtt_ = network->CreateMqtt(1); 
    if (!luki_mqtt_) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return;
    }

    luki_mqtt_->SetKeepAlive(keepalive);

    // 创建发布队列，用于异步上报
    publish_queue_ = xQueueCreate(5, sizeof(MqttQueueMessage));
    if (!publish_queue_) {
        ESP_LOGE(TAG, "Failed to create publish queue");
        return;
    }

    BaseType_t task_result = xTaskCreate(&LukiMqttService::TaskEntry, TAG, 4096, this, 5, &task_handle_);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT task");
        return;
    }

    ESP_LOGI(TAG, "MQTT service started successfully");
}

void LukiMqttService::Stop() {
    ESP_LOGI(TAG, "Stopping MQTT service for sleep mode...");
    
    // 发布离线状态
    if (luki_mqtt_ && luki_mqtt_->IsConnected()) {
        // ESP_LOGI(TAG, "Publishing offline status before sleep");
        // std::string sleep_message = CreateDeviceMessageJson("device_status", "DEVICE_SLEEPING");
        // luki_mqtt_->Publish("luki/device", sleep_message);
        // vTaskDelay(pdMS_TO_TICKS(500));  // 等待发布完成
        
        // 断开连接
        luki_mqtt_->Disconnect();
        ESP_LOGI(TAG, "MQTT connection closed for sleep");
    }
    
    // 既然连接关闭了，任务也应该停止以节省资源
    if (task_handle_) {
        ESP_LOGI(TAG, "Stopping MQTT task for sleep...");
        
        // 先标记队列为无效，让任务安全退出
        QueueHandle_t temp_queue = publish_queue_;
        publish_queue_ = nullptr;
        
        // 等待任务自然退出（最多等待3秒）
        bool task_ended = false;
        for (int i = 0; i < 30; i++) {
            if (eTaskGetState(task_handle_) == eDeleted) {
                task_ended = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        if (task_ended) {
            ESP_LOGI(TAG, "MQTT task exited gracefully");
        } else {
            ESP_LOGW(TAG, "Task didn't exit gracefully after 3s, force deleting...");
            vTaskDelete(task_handle_);
        }
        
        task_handle_ = nullptr;
        
        // 清理队列
        if (temp_queue) {
            vQueueDelete(temp_queue);
        }
        
        ESP_LOGI(TAG, "MQTT task and queue cleaned up for sleep");
    }
}

void LukiMqttService::Resume() {
    ESP_LOGI(TAG, "Resuming MQTT service from sleep mode...");
    
    // 如果任务和队列在睡眠时被清理了，需要重新创建
    if (!task_handle_ || !publish_queue_) {
        ESP_LOGI(TAG, "Recreating MQTT task and queue after sleep...");
        
        // 重新创建发布队列
        if (!publish_queue_) {
            publish_queue_ = xQueueCreate(5, sizeof(MqttQueueMessage));
            if (!publish_queue_) {
                ESP_LOGE(TAG, "Failed to recreate publish queue");
                return;
            }
        }
        
        // 重新创建任务
        if (!task_handle_) {
            BaseType_t task_result = xTaskCreate(&LukiMqttService::TaskEntry, "LukiMqtt", 4096, this, 5, &task_handle_);
            if (task_result != pdPASS) {
                ESP_LOGE(TAG, "Failed to recreate MQTT task");
                return;
            }
        }
        
        ESP_LOGI(TAG, "MQTT task and queue recreated successfully");
    }
    
    if (luki_mqtt_) {
        // 重新连接
        if (EnsureConnected()) {
            ESP_LOGI(TAG, "MQTT reconnected after sleep");
            // 发布上线状态
            std::string awake_message = CreateDeviceMessageJson("device_status", "DEVICE_AWAKE");
            luki_mqtt_->Publish("luki/device", awake_message);
        } else {
            ESP_LOGW(TAG, "Failed to reconnect MQTT after sleep");
        }
    }
}

void LukiMqttService::TaskEntry(void* arg) {
    LukiMqttService* service = static_cast<LukiMqttService*>(arg);
    if (service) {
        service->TaskLoop();
    }
    
    // 任务即将结束，确保不再访问任何成员变量
    ESP_LOGI(TAG, "MQTT task entry ending");
    vTaskDelete(nullptr);  // 主动删除自己，确保任务完全结束
}

bool LukiMqttService::EnsureConnected() {
    if (!luki_mqtt_) {
        ESP_LOGE(TAG, "MQTT client is null");
        return false;
    }

    if (endpoint_.empty()) {
        ESP_LOGE(TAG, "MQTT endpoint is empty");
        return false;
    }

    // 检查网络状态
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    if (!network) {
        ESP_LOGW(TAG, "Network interface not available");
        return false;
    }

    std::string broker_address;
    int broker_port = 8883;
    size_t pos = endpoint_.find(':');
    if (pos != std::string::npos) {
        broker_address = endpoint_.substr(0, pos);
        broker_port = std::stoi(endpoint_.substr(pos + 1));
    } else {
        broker_address = endpoint_;
    }

    ESP_LOGD(TAG, "Parsed broker: %s:%d", broker_address.c_str(), broker_port);

    // 如果未连接则尝试连接
    if (!luki_mqtt_->IsConnected()) {
        ESP_LOGI(TAG, "Connecting to MQTT %s:%d, client_id=%s", 
                broker_address.c_str(), broker_port, client_id_.c_str());
        
        // 添加连接超时检查
        TickType_t start_time = xTaskGetTickCount();
        bool connect_result = luki_mqtt_->Connect(broker_address, broker_port, client_id_, username_, password_);
        TickType_t connect_time = xTaskGetTickCount() - start_time;
        
        ESP_LOGI(TAG, "MQTT Connect() took %d ms, result=%s", 
                (int)(connect_time * portTICK_PERIOD_MS), connect_result ? "true" : "false");
        
        if (!connect_result) {
            ESP_LOGE(TAG, "MQTT connect failed - broker=%s:%d, client_id=%s", 
                    broker_address.c_str(), broker_port, client_id_.c_str());
            return false;
        }
        
        // 验证连接状态
        if (!luki_mqtt_->IsConnected()) {
            ESP_LOGE(TAG, "MQTT Connect() returned true but IsConnected() is false");
            return false;
        }
        
        ESP_LOGI(TAG, "MQTT connected successfully to %s:%d", broker_address.c_str(), broker_port);
    }
    return true;
}

void LukiMqttService::TaskLoop() {
    ESP_LOGI(TAG, "MQTT task started");
    
    // 初始连接尝试
    bool initial_connected = EnsureConnected();
    ESP_LOGI(TAG, "Initial connection result: %s", initial_connected ? "SUCCESS" : "FAILED");
    
    while (true) {
        // 检查队列是否仍然有效（防止析构时的竞态条件）
        if (!publish_queue_) {
            ESP_LOGW(TAG, "Queue is null, task exiting");
            break;
        }
        
        MqttQueueMessage queue_msg;
        
        // 等待异步消息，超时1秒
        if (xQueueReceive(publish_queue_, &queue_msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGD(TAG, "Processing message: %s", queue_msg.message_type);
            
            if (EnsureConnected()) {
                if (queue_msg.type == MqttMessageType::STATUS_PUBLISH) {
                    PublishStatusOnce();
                } else if (queue_msg.type == MqttMessageType::DEVICE_MESSAGE) {
                    PublishDeviceMessage(std::string(queue_msg.message_type), std::string(queue_msg.data));
                }
            }
        }
        
        // 再次检查队列有效性
        if (!publish_queue_) {
            ESP_LOGW(TAG, "Queue became null during operation, task exiting");
            break;
        }
    }
    
    ESP_LOGI(TAG, "MQTT task ended");
}

bool LukiMqttService::PublishStatusOnce() {
    if (!luki_mqtt_ || !luki_mqtt_->IsConnected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping status publish");
        return false;
    }

    // 获取设备状态信息
    auto& board = Board::GetInstance();
    std::string status_json = board.GetDeviceStatusJson();
    
    if (status_json.empty()) {
        ESP_LOGW(TAG, "Device status JSON is empty");
        return false;
    }

    // 使用统一的设备消息格式发布状态
    return PublishDeviceMessage("device_status", status_json);
}

void LukiMqttService::PublishStatusAsync() {
    if (!publish_queue_) {
        ESP_LOGW(TAG, "Publish queue not available");
        return;
    }
    
    MqttQueueMessage msg;
    msg.type = MqttMessageType::STATUS_PUBLISH;
    snprintf(msg.message_type, sizeof(msg.message_type), "device_status");
    msg.data[0] = '\0';  // 空字符串
    
    if (xQueueSend(publish_queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue status message");
    }
}



void LukiMqttService::PublishDeviceMessageAsync(const std::string& type, const cJSON* message_data) {
    if (!publish_queue_) {
        ESP_LOGW(TAG, "Publish queue not available");
        return;
    }

    char* json_string = cJSON_Print(message_data);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to serialize message data");
        return;
    }

    // 检查消息长度
    std::string json_str(json_string);
    if (json_str.length() >= sizeof(((MqttQueueMessage*)0)->data)) {
        ESP_LOGW(TAG, "Message too long (%d bytes), truncating", (int)json_str.length());
    }

    MqttQueueMessage msg;
    msg.type = MqttMessageType::DEVICE_MESSAGE;
    snprintf(msg.message_type, sizeof(msg.message_type), "%s", type.c_str());
    snprintf(msg.data, sizeof(msg.data), "%s", json_string);
    free(json_string);

    if (xQueueSend(publish_queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue device message: %s", type.c_str());
    }
}

void LukiMqttService::PublishDeviceMessageAsync(const std::string& type, const std::string& simple_message) {
    if (!publish_queue_) {
        ESP_LOGW(TAG, "Publish queue not available");
        return;
    }

    // 检查消息长度
    if (simple_message.length() >= sizeof(((MqttQueueMessage*)0)->data)) {
        ESP_LOGW(TAG, "Message too long (%d bytes), truncating", (int)simple_message.length());
    }

    MqttQueueMessage msg;
    msg.type = MqttMessageType::DEVICE_MESSAGE;
    snprintf(msg.message_type, sizeof(msg.message_type), "%s", type.c_str());
    snprintf(msg.data, sizeof(msg.data), "%s", simple_message.c_str());

    if (xQueueSend(publish_queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue device message: %s", type.c_str());
    }
}

bool LukiMqttService::PublishDeviceMessage(const std::string& type, const std::string& data) {
    if (!luki_mqtt_ || !luki_mqtt_->IsConnected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping device message");
        return false;
    }

    std::string full_message = CreateDeviceMessageJson(type, data);
    if (full_message.empty()) {
        ESP_LOGE(TAG, "Failed to create device message JSON");
        return false;
    }

    ESP_LOGI(TAG, "发布设备消息 [%s] 到 %s", type.c_str(), endpoint_.c_str());
    ESP_LOGD(TAG, "消息内容: %s", full_message.c_str());
    
    bool result = luki_mqtt_->Publish("luki/device", full_message);
    ESP_LOGI(TAG, "发布结果: %s", result ? "✅成功" : "❌失败");
    
    return result;
}

std::string LukiMqttService::CreateDeviceMessageJson(const std::string& type, const std::string& data) {
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE(TAG, "Failed to create JSON object for device message");
        return "";
    }

    // 添加设备基础信息
    cJSON_AddStringToObject(json, "mac_address", SystemInfo::GetMacAddress().c_str());
    
    std::string serial_number = GetSerialNumber();
    if (!serial_number.empty()) {
        cJSON_AddStringToObject(json, "serial_number", serial_number.c_str());
    }
    
    // cJSON_AddNumberToObject(json, "timestamp", (double)GetCurrentTimestamp());
    cJSON_AddStringToObject(json, "type", type.c_str());

    // 添加消息内容
    if (!data.empty()) {
        ESP_LOGD(TAG, "Processing message data: %s", data.c_str());
        cJSON* parsed_data = cJSON_Parse(data.c_str());
        if (parsed_data) {
            ESP_LOGD(TAG, "Successfully parsed data as JSON object");
            cJSON_AddItemToObject(json, "message", parsed_data);
        } else {
            ESP_LOGW(TAG, "Failed to parse data as JSON, treating as string");
            cJSON_AddStringToObject(json, "message", data.c_str());
        }
    } else {
        cJSON_AddObjectToObject(json, "message");
    }

    // 序列化并返回
    char* json_string = cJSON_Print(json);
    std::string result = json_string ? std::string(json_string) : "";
    
    if (json_string) free(json_string);
    cJSON_Delete(json);
    
    return result;
}

// time_t LukiMqttService::GetCurrentTimestamp() {
//     time_t now = time(NULL);
//     struct tm* tm_info = localtime(&now);
    
//     // 检查系统时间是否已设置（年份大于2024）
//     if (tm_info->tm_year >= 2024 - 1900) {
//         // 使用真实的Unix时间戳
//         return now;
//     } else {
//         // 系统时间未设置，使用启动时间 + 一个基准时间戳
//         // 2024-01-01 00:00:00 UTC = 1704067200
//         time_t estimated_time = 1704067200 + (esp_timer_get_time() / 1000000);
//         ESP_LOGW(TAG, "System time not set (year: %d), using estimated timestamp: %ld", 
//                  tm_info->tm_year + 1900, estimated_time);
//         return estimated_time;
//     }
// }

std::string LukiMqttService::GetSerialNumber() {
    std::string serial_number;
    
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    uint8_t serial_data[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_data, 32 * 8) == ESP_OK) {
        if (serial_data[0] != 0) {
            serial_number = std::string(reinterpret_cast<char*>(serial_data), 32);
            // 移除尾部的空字符
            serial_number = serial_number.c_str();
        }
    }
#endif
    
    return serial_number;
} 