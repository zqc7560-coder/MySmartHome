/**
 ******************************************************************************
 * @file    mqtt_client.h
 * @brief   轻量级 MQTT v3.1.1 客户端协议栈（位于 User 目录）。
 *
 * 功能：
 *   - CONNECT / CONNACK   连接与认证
 *   - PUBLISH (QoS0/QoS1) 发布与接收
 *   - SUBSCRIBE / SUBACK  订阅
 *   - PINGREQ / PINGRESP  心跳保活
 *   - DISCONNECT          主动断开
 *
 * 传输层分离：协议栈不关心底层是 ESP8266 透传、网卡还是虚拟串口，
 * 仅要求调用方在 mqtt_transport_t 中提供“发送”和“接收”两个回调。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __MQTT_CLIENT_H
#define __MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* 传输层回调（由调用方实现，接入 ESP8266 透传等底层通道）                     */
/* ------------------------------------------------------------------------- */
typedef int (*mqtt_send_fn)(const uint8_t *data, uint16_t len);
typedef int (*mqtt_recv_fn)(uint8_t *data, uint16_t max_len, uint32_t timeout_ms);

typedef struct
{
    mqtt_send_fn send;      /**< 发送 len 字节，返回成功与否 */
    mqtt_recv_fn recv;      /**< 接收至多 max_len 字节，带超时，返回实际字节数 */
} mqtt_transport_t;

/* ------------------------------------------------------------------------- */
/* 接收数据回调上下文（one-shot）                                              */
/* ------------------------------------------------------------------------- */
/** 收到服务器 PUBLISH 时的回调（QoS1 时协议栈自动回 PUBACK） */
typedef void (*mqtt_message_cb)(const char *topic, const uint8_t *payload, uint16_t plen);

/* ------------------------------------------------------------------------- */
/* 客户端对象                                                                  */
/* ------------------------------------------------------------------------- */
#define MQTT_TX_BUF_SIZE   320U   /* 发送缓冲（容纳 CONNECT/PUBLISH/SUBSCRIBE） */
#define MQTT_RX_BUF_SIZE   256U   /* 接收缓冲（容纳 CONNACK/SUBACK/PUBLISH）    */

typedef struct
{
    mqtt_transport_t  t;                 /**< 传输层回调 */
    mqtt_message_cb   on_message;        /**< 消息到达回调 */
    uint16_t          next_pkt_id;       /**< 递增报文标识符 */

    /* 收发缓冲 */
    uint8_t           txbuf[MQTT_TX_BUF_SIZE];
    uint16_t          txlen;

    /* 接收解析中间状态 */
    uint8_t           rxbuf[MQTT_RX_BUF_SIZE];
    uint8_t           pending_qos;
} mqtt_client_t;

/* CONNACK 返回码 */
enum
{
    MQTT_CONN_ACCEPTED        = 0x00,
    MQTT_CONN_REFUSED_PROTO   = 0x01,   /* 协议版本不支持 */
    MQTT_CONN_REFUSED_ID      = 0x02,   /* 客户端 ID 非法 */
    MQTT_CONN_REFUSED_UNAVAIL = 0x03,   /* 服务器不可用 */
    MQTT_CONN_REFUSED_BAD_USER= 0x04,   /* 用户名/密码错误 */
    MQTT_CONN_REFUSED_NOT_AUTH= 0x05    /* 未授权 */
};

/* ------------------------------------------------------------------------- */
/* 接口函数                                                                    */
/* ------------------------------------------------------------------------- */

/** 初始化客户端对象，指定传输层回调与消息回调 */
void mqtt_client_init(mqtt_client_t *c, const mqtt_transport_t *t, mqtt_message_cb cb);

/**
 * 发送 CONNECT 并等待 CONNACK。
 * @param clientid 客户端标识（OneNet 用 = 设备 ID）
 * @param username 用户名（OneNet 用 = 产品 ID）
 * @param password 密码  （OneNet 用 = 产品 APIKey/访问凭证）
 * @param keepalive 心跳间隔秒数
 * @return 0 连接成功；负值协议层错误；>0 为 CONNACK 拒连接返回码
 */
int mqtt_connect(mqtt_client_t *c,
                 const char *clientid, const char *username, const char *password,
                 uint16_t keepalive);

/** 发布消息，支持 QoS0/QoS1。返回 0 成功。 */
int mqtt_publish(mqtt_client_t *c, const char *topic,
                 const uint8_t *payload, uint16_t plen, uint8_t qos);

/** 订阅主题，QoS0/1。返回报文标识符（>0 成功）。 */
uint16_t mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos);

/** 发送心跳 PINGREQ。返回 0 成功。 */
int mqtt_ping(mqtt_client_t *c);

/** 主动发送 DISCONNECT。 */
void mqtt_disconnect(mqtt_client_t *c);

/**
 * 轮询接收一个 MQTT 报文（含解析分发）：
 *   - CONNACK/SUBACK/PINGRESP 内部处理并返回类型；
 *   - PUBLISH 触发 on_message 回调；QoS1 自动回 PUBACK；
 * @param timeout_ms 单字节接收超时
 * @return >0 收到报文类型（MQTT_PKT_*）；0 超时；<0 错误
 */
int mqtt_poll(mqtt_client_t *c, uint32_t timeout_ms);

/** MQTT 报文类型宏（便于调用方识别 poll 返回） */
#define MQTT_PKT_CONNACK   0x20U
#define MQTT_PKT_PUBACK    0x40U
#define MQTT_PKT_SUBACK    0x90U
#define MQTT_PKT_PUBLISH   0x30U
#define MQTT_PKT_PINGRESP  0xD0U
#define MQTT_PKT_UNKNOWN   0xFFU

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_CLIENT_H */