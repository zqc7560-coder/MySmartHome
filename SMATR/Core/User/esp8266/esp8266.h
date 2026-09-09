/**
 ******************************************************************************
 * @file    esp8266.h
 * @brief   ESP8266 AT 指令驱动模块（USART2）：
 *          - AT 指令交互（测试 / 复位 / 模式设置）；
 *          - WiFi 连接（Station 模式，CWJAP）；
 *          - TCP 连接与“透传模式”建立（CIPSTART / CIPMODE / CIPSEND）；
 *          - 透传数据的收发（供上层 MQTT 协议栈直接使用）。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __ESP8266_H
#define __ESP8266_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* 常量配置                                                                    */
/* ------------------------------------------------------------------------- */
#define ESP_CMD_TIMEOUT_DEF   3000U   /* AT 指令默认响应超时(ms) */
#define ESP_JOIN_TIMEOUT      15000U  /* 连接 AP 最大等待(ms) */

/* WiFi 工作模式 */
#define ESP_MODE_STATION      1U      /* 仅 Station                 */
#define ESP_MODE_AP           2U
#define ESP_MODE_STA_AP       3U

/* ------------------------------------------------------------------------- */
/* 接口函数                                                                    */
/* ------------------------------------------------------------------------- */

/** ESP8266 芯片自检：发送 "AT"，等待 "OK"。失败返回 false */
bool esp_test(uint32_t timeout_ms);

/** 初始化：发送 "AT" 校验后关闭回显（ATE0），避免解析受回显干扰。 */
bool esp_at_init(uint32_t timeout_ms);

/**
 * 波特率自动扫描：依次用常见波特率重设 USART2 并发送 "AT"，
 * 收到 "OK" 的那个波特率即模块当前工作波特率（用于模块波特率未知/不匹配时）。
 * @param out_baud  可选：输出最终匹配到的波特率（非空时写入）
 * @return true 找到匹配波特率；false 所有候选均无 "OK"（可能非 AT 固件或链路不通）
 */
bool esp_autobaud(uint32_t *out_baud);

/** 软件复位：AT+RST，并等待固件就绪（"ready"）。 */
bool esp_reset(uint32_t timeout_ms);

/** 设置 WiFi 工作模式（如 Station）。返回是否成功。 */
bool esp_set_mode(uint32_t mode, uint32_t timeout_ms);

/** 连接指定 AP（AT+CWJAP），等待 "WIFI CONNECTED" 与 "WIFI GOT IP"。 */
bool esp_join_ap(const char *ssid, const char *pass, uint32_t timeout_ms);

/**
 * 使用 TCP 方式连接服务器并进入透传模式：
 *     AT+CIPSTART="TCP",<host>,<port>   → 建立连接
 *     AT+CIPMODE=1                      → 设置透传模式
 *     AT+CIPSEND                        → 等待 ">" 后进入透传
 * 成功后返回 true；此后再收发均为原始字节流。
 */
bool esp_open_transparent(const char *host, uint16_t port, uint32_t timeout_ms);

/** 断开当前 TCP 连接（AT+CIPCLOSE），回到普通指令模式。 */
void esp_close_transparent(void);

/** 透传模式下发送原始数据。返回实际发送长度（0 表示失败）。 */
uint16_t esp_trans_send(const uint8_t *data, uint16_t len);

/**
 * 透传模式下接收原始数据。
 * @param buf      接收缓冲
 * @param max_len  最大长度
 * @param timeout_ms 等待超时时间；超时返回已收到的字节数（可能为 0）。
 * @return 实际接收字节数
 */
uint16_t esp_trans_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

/** 是否已建立透传连接（内部状态） */
bool esp_is_transparent(void);

/**
 * 强制退出透传回到指令模式（连接异常中断后用于恢复）。
 * 无论是否处于透传模式调用均安全：
 *   - 透传模式中发 "+++"（需前后静默）让固件回退；
 *   - 指令模式中发送 "+++" 无害，随后统一清空接收缓冲。
 * 必须在后续任何 AT 指令之前调用，否则 AT 会被当作透传数据而非指令解析。
 */
void esp_force_command_mode(void);

/** 清空接收缓冲（丢弃残余数据） */
void esp_flush_rx(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_H */