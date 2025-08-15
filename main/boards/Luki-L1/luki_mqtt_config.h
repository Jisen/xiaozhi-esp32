#ifndef LUKI_MQTT_CONFIG_H
#define LUKI_MQTT_CONFIG_H

// MQTT 配置常量
// 可以通过编译时定义或环境变量覆盖这些默认值

#ifndef LUKI_MQTT_ENDPOINT
#define LUKI_MQTT_ENDPOINT "120.78.190.249:1883"  // 标准MQTT端口，无加密
#endif

#ifndef LUKI_MQTT_CLIENT_ID_PREFIX
#define LUKI_MQTT_CLIENT_ID_PREFIX "luki_l1"
#endif

#ifndef LUKI_MQTT_USERNAME
#define LUKI_MQTT_USERNAME ""  // 空字符串表示不使用认证
#endif

#ifndef LUKI_MQTT_PASSWORD
#define LUKI_MQTT_PASSWORD ""  // 空字符串表示不使用认证
#endif

#ifndef LUKI_MQTT_STATUS_TOPIC
#define LUKI_MQTT_STATUS_TOPIC "luki/status"  // 使用独特的主题避免冲突
#endif

#ifndef LUKI_MQTT_STATUS_INTERVAL
#define LUKI_MQTT_STATUS_INTERVAL 120  // 秒
#endif

#ifndef LUKI_MQTT_KEEPALIVE
#define LUKI_MQTT_KEEPALIVE 240  // 秒
#endif

// 安全选项：是否在日志中隐藏敏感信息
#ifndef LUKI_MQTT_HIDE_CREDENTIALS
#define LUKI_MQTT_HIDE_CREDENTIALS 1
#endif

#endif // LUKI_MQTT_CONFIG_H 