# 显示系统优化说明

## 问题描述

原有的显示系统在刷新时占用过多CPU资源，导致网络通信中断和系统响应缓慢。主要问题包括：

1. **同步显示更新**：显示操作在主线程中同步执行，阻塞网络通信
2. **高优先级LVGL任务**：LVGL任务优先级过高，抢占网络任务资源
3. **频繁刷新**：50ms的刷新间隔导致CPU占用过高

## 解决方案

### 1. 异步显示刷新线程

#### 实现原理
- 创建独立的低优先级显示刷新线程（优先级0）
- 使用消息队列进行异步通信
- 主线程只负责发送显示更新请求，不等待执行完成

#### 关键特性
```cpp
#define DISPLAY_TASK_PRIORITY 0  // 最低优先级
#define DISPLAY_REFRESH_DELAY_MS 5  // 显示刷新间隔
#define DISPLAY_QUEUE_SIZE 10  // 消息队列大小
```

#### 线程配置
- **任务优先级**：0（最低优先级，确保不影响网络通信）
- **核心绑定**：固定到核心1，避免与主任务冲突
- **栈大小**：4KB，适中的内存占用
- **消息队列**：非阻塞发送，队列满时丢弃旧消息

### 2. LVGL系统优化

#### 优化参数
```cpp
port_cfg.task_priority = 0;      // 降低LVGL任务优先级
port_cfg.timer_period_ms = 100;  // 增加刷新间隔到100ms
```

#### 优化效果
- 减少LVGL任务对网络通信的影响
- 降低整体CPU占用率
- 保持显示效果的同时提升系统响应性

### 3. 消息队列机制

#### 消息类型
```cpp
enum DisplayUpdateType {
    DISPLAY_UPDATE_EMOTION,      // 表情更新
    DISPLAY_UPDATE_CHAT_MESSAGE, // 聊天消息更新
    DISPLAY_UPDATE_ICON         // 图标更新
};
```

#### 异步处理流程
1. 主线程调用显示更新方法
2. 将更新请求打包成消息
3. 非阻塞方式发送到消息队列
4. 显示线程异步处理消息
5. 在显示线程中执行实际的LVGL操作

## 性能优化效果

### CPU占用优化
- **LVGL任务优先级**：从1降低到0
- **刷新间隔**：从50ms增加到100ms
- **显示线程优先级**：0（最低优先级）
- **显示操作延迟**：增加5ms延迟，减少CPU抢占

### 网络通信保障
- 主事件循环优先级：3（高优先级）
- 显示相关任务优先级：0（低优先级）
- 异步处理避免阻塞网络操作

### 内存使用优化
- 消息队列大小：10个消息
- 每个消息最大256字节内容
- 显示线程栈：4KB

## 使用方法

### 异步显示更新
```cpp
// 这些方法现在是异步的，不会阻塞调用线程
display->SetEmotion("happy");
display->SetChatMessage("user", "Hello World");
display->SetIcon("download");
```

### 线程安全
- 所有LVGL操作都在显示线程中执行
- 使用DisplayLockGuard确保线程安全
- 消息队列提供线程间通信

## 注意事项

1. **消息丢弃**：当消息队列满时，新消息会被丢弃，确保系统不会因显示更新而阻塞
2. **延迟显示**：显示更新现在是异步的，可能有轻微延迟
3. **优先级设置**：显示线程使用最低优先级，确保网络通信优先

## 监控和调试

### 日志输出
```
I CustomEmojiDisplay: Display refresh thread started with priority 0
I CustomEmojiDisplay: 设置表情: happy
W CustomEmojiDisplay: Display queue full, dropping emotion update
```

### 性能监控
- 显示线程优先级会在启动时输出
- 队列满时会有警告日志
- 可以通过SystemInfo监控任务CPU使用情况

这个优化方案确保了显示系统不会影响关键的网络通信功能，同时保持良好的用户体验。 