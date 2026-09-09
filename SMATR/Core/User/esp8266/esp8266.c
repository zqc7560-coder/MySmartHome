/**
 ******************************************************************************
 * @file    esp8266.c
 * @brief   ESP8266 AT 指令驱动模块的实现。
 ******************************************************************************
 */
#include "esp8266/esp8266.h"
#include "uart/uart_config.h"
#include "port/usart_irq.h"
#include "common/ringbuf.h"
#include "common/delay.h"
#include <string.h>
#include <stdio.h>   /* snprintf */

/* 本地发送缓冲（拼接 AT 指令 + \r\n） */
static char s_cmd_buf[128];

/* 透传状态标记 */
static bool s_transparent = false;

/* ------------------------------------------------------------------------- */
/* 内部：把字符串原样发送到 ESP8266 串口                                     */
/* ------------------------------------------------------------------------- */
static void esp_send_str(const char *s)
{
    uart_send(&huart_esp, (const uint8_t *)s, (uint16_t)strlen(s));
}

/* ------------------------------------------------------------------------- */
/* 内部：等待某个字符串模式出现在接收缓冲中；成功后丢弃到该模式结尾。        */
/* ------------------------------------------------------------------------- */
static bool esp_wait_pattern(const char *pat, uint32_t timeout_ms)
{
    static uint8_t pattern[16];
    uint16_t plen = (uint16_t)strlen(pat);
    if (plen == 0 || plen > sizeof(pattern))
    {
        return false;
    }
    memcpy(pattern, pat, plen);

    timeout_t timer;
    timeout_start(&timer, timeout_ms);
    do
    {
        int16_t idx = ringbuf_search(&g_rb_esp_rx, pattern, plen);
        if (idx >= 0)
        {
            /* 丢弃模式的偏移位置以及模式本身，并顺带清掉可能紧随的 \r\n */
            ringbuf_discard(&g_rb_esp_rx, (uint16_t)(idx + plen));
            return true;
        }
        delay_ms(2);
    } while (!timeout_expired(&timer));

    return false;
}

/* ------------------------------------------------------------------------- */
/* 内部：等待多个模式中的任意一个；命中后把索引写入 out_idx 并消费该模式。   */
/* ------------------------------------------------------------------------- */
static bool esp_wait_any(const char **pats, uint8_t n, int8_t *out_idx, uint32_t timeout_ms)
{
    static uint8_t pats_bin[4][16];

    timeout_t timer;
    timeout_start(&timer, timeout_ms);
    do
    {
        for (uint8_t i = 0; i < n; i++)
        {
            uint16_t plen = (uint16_t)strlen(pats[i]);
            if (plen == 0 || plen > 16)
            {
                continue;
            }
            memcpy(pats_bin[i], pats[i], plen);
            int16_t idx = ringbuf_search(&g_rb_esp_rx, pats_bin[i], plen);
            if (idx >= 0)
            {
                ringbuf_discard(&g_rb_esp_rx, (uint16_t)(idx + plen));
                if (out_idx != NULL)
                {
                    *out_idx = (int8_t)i;
                }
                return true;
            }
        }
        delay_ms(2);
    } while (!timeout_expired(&timer));

    if (out_idx != NULL)
    {
        *out_idx = -1;
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* 内部：发送一条指令并等待 "OK"(成功) / "ERROR"(失败)。                      */
/* ------------------------------------------------------------------------- */
static bool esp_cmd(const char *cmd, uint32_t timeout_ms)
{
    static const char *reachable[] = {"OK", "ERROR"};
    /* 先把之前残留的清掉，避免读到过期应答 */
    ringbuf_flush(&g_rb_esp_rx);

    esp_send_str(cmd);
    esp_send_str("\r\n");

    int8_t idx;
    if (esp_wait_any(reachable, 2, &idx, timeout_ms))
    {
        return (idx == 0);   /* 0=OK 成功，1=ERROR 失败 */
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
bool esp_test(uint32_t timeout_ms)
{
    return esp_cmd("AT", timeout_ms);
}

bool esp_at_init(uint32_t timeout_ms)
{
    if (!esp_cmd("AT", timeout_ms))
    {
        return false;
    }
    /* 关闭回显，避免后续应答解析受到命令回显干扰 */
    return esp_cmd("ATE0", timeout_ms);
}

bool esp_autobaud(uint32_t *out_baud)
{
    /* 常见 ESP8266 AT 固件波特率候选 */
    static const uint32_t candidates[] = {115200U, 9600U, 57600U, 38400U, 230400U, 460800U};
    static const char *reachable[] = {"OK", "ERROR"};

    for (uint32_t i = 0; i < (uint32_t)(sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        /* 以候选波特率重设 USART2 并重新挂起接收中断 */
        uart_esp_set_baud(candidates[i]);
        usart_esp_rx_rearm();
        ringbuf_flush(&g_rb_esp_rx);

        /* 发送 "AT\r\n"，等待 OK/ERROR */
        esp_send_str("AT");
        esp_send_str("\r\n");

        int8_t idx;
        if (esp_wait_any(reachable, 2, &idx, 500))
        {
            if (out_baud != NULL)
            {
                *out_baud = candidates[i];
            }
            return true;
        }
    }
    return false;
}

bool esp_reset(uint32_t timeout_ms)
{
    /* 复位后固件会输出 "ready" */
    ringbuf_flush(&g_rb_esp_rx);
    esp_send_str("AT+RST\r\n");
    /* 复位后可能需要更长等待；用传入的超时等待 "ready" */
    bool ok = esp_wait_pattern("ready", timeout_ms);
    delay_ms(200);   /* 等待固件稳定 */
    return ok;
}

bool esp_set_mode(uint32_t mode, uint32_t timeout_ms)
{
    if (mode < ESP_MODE_STATION)
    {
        return false;
    }
    s_cmd_buf[0] = '\0';
    snprintf(s_cmd_buf, sizeof(s_cmd_buf), "AT+CWMODE=%u", (unsigned)mode);
    return esp_cmd(s_cmd_buf, timeout_ms);
}

bool esp_join_ap(const char *ssid, const char *pass, uint32_t timeout_ms)
{
    static const char *expected[] = {"WIFI GOT IP", "ERROR"};
    if (ssid == NULL || pass == NULL)
    {
        return false;
    }
    s_cmd_buf[0] = '\0';
    snprintf(s_cmd_buf, sizeof(s_cmd_buf), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);

    ringbuf_flush(&g_rb_esp_rx);
    esp_send_str(s_cmd_buf);
    esp_send_str("\r\n");

    int8_t idx;
    if (esp_wait_any(expected, 2, &idx, timeout_ms))
    {
        /* 顺带等待协商完成，稳定连接 */
        delay_ms(100);
        return (idx == 0);   /* GOT IP 成功，ERROR 失败 */
    }
    return false;
}

bool esp_open_transparent(const char *host, uint16_t port, uint32_t timeout_ms)
{
    /* 1) 建立 TCP 连接 */
    s_cmd_buf[0] = '\0';
    snprintf(s_cmd_buf, sizeof(s_cmd_buf), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, (unsigned)port);
    if (!esp_cmd(s_cmd_buf, timeout_ms))
    {
        return false;
    }

    /* 2) 设置透传模式 */
    if (!esp_cmd("AT+CIPMODE=1", timeout_ms))
    {
        return false;
    }

    /* 3) 进入透传（等待 ">" 提示符） */
    ringbuf_flush(&g_rb_esp_rx);
    esp_send_str("AT+CIPSEND\r\n");
    if (!esp_wait_pattern(">", timeout_ms))
    {
        return false;
    }

    s_transparent = true;
    delay_ms(100);
    return true;
}

void esp_close_transparent(void)
{
    /* 在透传模式下先发 "+++" 返回指令模式（须前后各留 1s 静默） */
    if (s_transparent)
    {
        delay_ms(1000);
        esp_send_str("+++");
        delay_ms(1100);
        esp_send_str("AT+CIPCLOSE\r\n");
        (void)esp_cmd("AT", 1000);   /* 确认已回到指令模式 */
        s_transparent = false;
    }
    else
    {
        esp_send_str("AT+CIPCLOSE\r\n");
    }
    ringbuf_flush(&g_rb_esp_rx);
}

uint16_t esp_trans_send(const uint8_t *data, uint16_t len)
{
    if (!s_transparent || data == NULL || len == 0)
    {
        return 0;
    }
    return uart_send(&huart_esp, data, len);
}

uint16_t esp_trans_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    if (!s_transparent || buf == NULL || max_len == 0)
    {
        return 0;
    }

    /* 先尝试立即取回当前缓冲中的数据 */
    uint16_t got = ringbuf_peek_buf(&g_rb_esp_rx, buf, max_len);
    if (got > 0)
    {
        ringbuf_discard(&g_rb_esp_rx, got);
        return got;
    }

    /* 等待数据到达（带超时） */
    timeout_t timer;
    timeout_start(&timer, timeout_ms);
    do
    {
        got = ringbuf_peek_buf(&g_rb_esp_rx, buf, max_len);
        if (got > 0)
        {
            ringbuf_discard(&g_rb_esp_rx, got);
            return got;
        }
        delay_ms(2);
    } while (!timeout_expired(&timer));

    return 0;
}

bool esp_is_transparent(void)
{
    return s_transparent;
}

void esp_force_command_mode(void)
{
    /* +++ 透传转指令模式的退出序列：前后需保持一段静默时间，固件才识别。 */
    static const uint8_t esc[] = { '+', '+', '+' };

    delay_ms(1100);                 /* 退出静默期：确保 +++ 前无其他数据 */
    uart_send(&huart_esp, esc, 3);  /* 连续发送三个 '+'（无 \r\n） */
    delay_ms(1200);                 /* 等待固件识别并退回到指令模式 */
    ringbuf_flush(&g_rb_esp_rx);    /* 丢弃残留应答/透传数据，避免干扰后续 AT 解析 */
    s_transparent = false;
}

void esp_flush_rx(void)
{
    ringbuf_flush(&g_rb_esp_rx);
}