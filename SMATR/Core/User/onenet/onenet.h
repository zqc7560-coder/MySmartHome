/**
 ******************************************************************************
 * @file    onenet.h
 * @brief   OneNet 云平台接入模块（MQTT over ESP8266）。
 *
 * 采用 MQTT 协议接入 OneNet（中移物联网）标准接入点：
 *     Broker : mqtts.heclouds.com
 *     Port   : 1883
 *     ClientID : 设备 ID (device_id)
 *     Username : 产品 ID (product_id)
 *     Password : 产品 APIKey / 访问凭证 (api_key)
 *
 * 功能：
 *   - one_net 连接流程：ESP 复位 → 联网 → TCP 透传 → MQTT CONNECT；
 *   - 数据点上报（$dp 主题，JSON 格式 datastreams/apipoints）；
 *   - 订阅命令下发主题 $creq/{device_id} 接收云端指令；
 *   - 心跳保活（PINGREQ / PINGRESP）与断线检测。
 ******************************************************************************
 */
#ifndef __ONENET_H
#define __ONENET_H

#include <stdint.h>
#include <stdbool.h>
#include "mqtt/mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* OneNet 账号配置（请按实际在 OneNet 控制台创建的信息填写）                   */
/* ------------------------------------------------------------------------- */
#define ONENET_PRODUCT_ID     "66R385U8lq"    /* 产品 ID */
#define ONENET_DEVICE_ID      "device"     /* 设备 ID(一般为 16 位数字) */
#define ONENET_API_KEY        "version=2018-10-31&res=products%2F66R385U8lq%2Fdevices%2Fdevice&et=1893456000&method=sha1&sign=6%2BfJmOttlIdB6aiBf2S2dj3XVkE%3D"       /* OneNet MQTT 的 password = 用设备key算出的访问token（非明文key） */

/* WiFi 配置（可用调试指令 "wifi" 运行时修改） */
#define ONENET_WIFI_SSID      "zz"
#define ONENET_WIFI_PASS      "zhuqicheng"

/* MQTT Broker：新版 OneNet 非加密接入地址为 studio-mqtt.heclouds.com:1883
   （旧地址 mqtts.heclouds.com 连得上认证也过，但新版控制台不认该会话，
    表现为设备离线且 $dp 不回 ACK。故改为新版标准接入点。） */
#define ONENET_BROKER_HOST    "studio-mqtt.heclouds.com"
#define ONENET_BROKER_PORT    1883
#define ONENET_KEEPALIVE      30U                  /* 心跳间隔(秒) */

/* 命令下发订阅主题（旧命令通道，兼容保留） */
#define ONENET_CMD_TOPIC      "$creq/" ONENET_DEVICE_ID

/* 新版物模型：属性上报/平台设置属性的系统主题 */
#define ONENET_PROP_POST_TOPIC  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/post"
#define ONENET_PROP_SET_TOPIC   "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/set"
#define ONENET_PROP_SET_REPLY   "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/set_reply"
#define ONENET_PROP_GET_TOPIC   "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/get"
#define ONENET_PROP_GET_REPLY   "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/get_reply"

/* LED / 温度 在物模型中的属性标识符（需与平台物模型创建的 identifier 完全一致） */
#define ONENET_LED_PROP_ID      "led"
#define ONENET_TEMP_PROP_ID     "EnvironmentTemperature"

/* ------------------------------------------------------------------------- */
/* 云端消息回调：收到 $creq/{device_id} 下发的命令时回调                        */
/* ------------------------------------------------------------------------- */
typedef void (*onenet_message_cb)(const char *topic, const uint8_t *payload, uint16_t plen);

/* ------------------------------------------------------------------------- */
/* 接口函数                                                                    */
/* ------------------------------------------------------------------------- */

/** 初始化 OneNet 模块（注册消息回调），此函数不发起连接 */
void onenet_init(onenet_message_cb cb);

/** 运行时修改 WiFi 账号密码（供 "wifi <ssid> <pass>" 调试指令使用） */
void onenet_set_wifi(const char *ssid, const char *pass);

/** 注册云端消息回调（可多次调用以更新） */
void onenet_set_message_cb(onenet_message_cb cb);

/**
 * 建立与 OneNet 的连接：联网 → TCP 透传 → MQTT CONNECT。
 * @return 0 成功；<0 失败（详见 onenet 错误码）
 */
int onenet_connect(void);

/** 断开连接（主动） */
void onenet_disconnect(void);

/** 当前连接状态 */
bool onenet_is_connected(void);

/**
 * 上报数值型数据点到 OneNet（写主题 $dp）。
 * @param dsid  数据流 ID（如 "temperature"）
 * @param value 数值
 * @return 0 成功
 */
int onenet_publish_dp(const char *dsid, float value);

/**
 * 上报字符串数据点到 OneNet（写主题 $dp）。
 * @param dsid 数据流 ID
 * @param str  字符串内容
 */
int onenet_publish_dp_str(const char *dsid, const char *str);

/** 向指定自定义主题发布原始负载（QoS） */
int onenet_publish_raw(const char *topic, const uint8_t *payload, uint16_t plen, uint8_t qos);

/**
 * 诊断用：以 QoS1 发布单个数据点并等待平台回 PUBACK。
 * @return 0=平台确认收到；1=发布后未收到 PUBACK(连接可能断开)；<0 错误
 */
int onenet_publish_wait_ack(const char *dsid, float value);

/** 订阅命令下发主题 */
int onenet_subscribe_cmd(void);

/** 新版物模型：上报单个属性到 thing/property/post（标识符需与物模型一致） */
int onenet_report_property(const char *idstr, float value);

/** 新版物模型：订阅平台属性设置主题 thing/property/set（用于云端控制LED等） */
int onenet_subscribe_property_set(void);

/** 新版物模型：订阅平台属性获取主题 thing/property/get（平台读当前属性） */
int onenet_subscribe_property_get(void);

/** 新版物模型：回传属性获取应答接口，把当前温度与LED状态回给平台 */
int onenet_thing_property_get_reply(const char *pkt_id, float temp, int led);

/** 新版物模型：回传属性设置应答到 thing/property/set_reply */
int onenet_reply_property_set(const char *pkt_id, int code, const char *msg);

/** 连接活性探针（诊断用）：发送 PINGREQ 等待回复。
 * @return 1=连接活着；0=无回复(链路已断)；<0 未连接/发送失败
 */
int onenet_probe(void);

/** 距最近一次收到平台报文已过的毫秒数（未连接返回 0xFFFFFFFF），供看门狗自检 */
uint32_t onenet_platform_rx_age_ms(void);

/**
 * 周期服务函数：负责接收/解析来自云端的报文，并在心跳超时后发送 PINGREQ。
 * 需在主循环中高频调用（如 10ms 一次）。
 */
void onenet_yield(void);

/* OneNet / 连接错误码 */
enum
{
    ONENET_OK                = 0,
    ONENET_ERR_ESP           = -1,   /* ESP8266 底层失败 */
    ONENET_ERR_JOIN_AP       = -2,   /* 连接 WiFi 失败 */
    ONENET_ERR_TCP           = -3,   /* 建立 TCP 透传失败 */
    ONENET_ERR_MQTT_CONN     = -4,   /* MQTT 连接被拒（返回值含具体码） */
    ONENET_ERR_NOT_CONNECTED = -5    /* 尚未连接，无法执行操作 */
};

#ifdef __cplusplus
}
#endif

#endif /* __ONENET_H */
