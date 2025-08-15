#ifndef LUKI_MQTT_SERVICE_H
#define LUKI_MQTT_SERVICE_H

#include <string>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <cJSON.h>

// 消息队列结构
enum class MqttMessageType {
    STATUS_PUBLISH,     // 状态发布
    DEVICE_MESSAGE      // 设备消息
};

struct MqttQueueMessage {
    MqttMessageType type;
    std::string message_type;    // device_status, session_status 等
    std::string data;           // JSON 字符串
};

class Mqtt;

class LukiMqttService {
public:
    LukiMqttService();
    ~LukiMqttService();

    void Start();
    void Stop();  // 停止服务并断开连接
    void Resume();  // 恢复服务并重新连接
    // 异步发布方法
    void PublishStatusAsync();  // 异步上报设备状态
    void PublishDeviceMessageAsync(const std::string& type, const cJSON* message_data);  // 异步发布JSON消息
    void PublishDeviceMessageAsync(const std::string& type, const std::string& simple_message);  // 异步发布简单消息

private:
    static void TaskEntry(void* arg);
    void TaskLoop();
    bool EnsureConnected();
    std::string GetSerialNumber();
    bool PublishStatusOnce();  // 发布设备状态
    bool PublishDeviceMessage(const std::string& type, const std::string& data);  // 同步发布设备消息
    std::string CreateDeviceMessageJson(const std::string& type, const std::string& data);  // 创建设备消息JSON

private:
    std::unique_ptr<Mqtt> luki_mqtt_;
    TaskHandle_t task_handle_ = nullptr;
    QueueHandle_t publish_queue_ = nullptr;
    std::string endpoint_;
    std::string client_id_;
    std::string username_;
    std::string password_;
};

#endif // LUKI_MQTT_SERVICE_H 