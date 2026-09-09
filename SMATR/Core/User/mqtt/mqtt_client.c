/**
 ******************************************************************************
 * @file    mqtt_client.c
 * @brief   轻量级 MQTT v3.1.1 客户端协议栈实现。
 ******************************************************************************
 */
#include "mqtt/mqtt_client.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* 内部基础工具                                                                */
/* ------------------------------------------------------------------------- */

/** 逐字节追加到发送缓冲 */
static void tx_put_byte(mqtt_client_t *c, uint8_t b)
{
    if (c->txlen < MQTT_TX_BUF_SIZE)
    {
        c->txbuf[c->txlen++] = b;
    }
}

/** 追加 2 字节大端整数 */
static void tx_put_u16(mqtt_client_t *c, uint16_t v)
{
    tx_put_byte(c, (uint8_t)(v >> 8));
    tx_put_byte(c, (uint8_t)(v & 0xFF));
}

/** 追加 MQTT 定长字符串（2 字节长度 + 内容） */
static void tx_put_str(mqtt_client_t *c, const char *s)
{
    uint16_t n = (s != NULL) ? (uint16_t)strlen(s) : 0;
    tx_put_u16(c, n);
    for (uint16_t i = 0; i < n; i++)
    {
        tx_put_byte(c, (uint8_t)s[i]);
    }
}

/** 写入 MQTT 变长剩余长度（最长 4 字节编码） */
static void tx_put_remaining(mqtt_client_t *c, uint32_t rem)
{
    do
    {
        uint8_t b = (uint8_t)(rem % 128);
        rem /= 128;
        if (rem > 0)
        {
            b |= 0x80;
        }
        tx_put_byte(c, b);
    } while (rem > 0);
}

/** 通过传输层把发送缓冲内容发出 */
static int tx_flush(mqtt_client_t *c)
{
    int ok = c->t.send(c->txbuf, c->txlen);
    c->txlen = 0;
    return ok;
}

/** 从传输层读单字节（阻塞至收到或超时） */
static int rx_byte(mqtt_client_t *c, uint8_t *b, uint32_t timeout_ms)
{
    return c->t.recv(b, 1, timeout_ms);   /* 返回 1 成功，0 超时 */
}

/** 读取 MQTT 变长剩余长度 */
static int rx_remaining_len(mqtt_client_t *c, uint32_t *out)
{
    uint32_t mult = 1;
    uint32_t val  = 0;
    for (int i = 0; i < 4; i++)
    {
        uint8_t b;
        if (rx_byte(c, &b, 2000) != 1)
        {
            return -1;
        }
        val += (uint32_t)(b & 0x7F) * mult;
        mult *= 128;
        if ((b & 0x80) == 0)
        {
            *out = val;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
void mqtt_client_init(mqtt_client_t *c, const mqtt_transport_t *t, mqtt_message_cb cb)
{
    memset(c, 0, sizeof(mqtt_client_t));
    if (t != NULL)
    {
        c->t = *t;
    }
    c->on_message   = cb;
    c->next_pkt_id  = 1;
    c->txlen        = 0;
    c->pending_qos  = 0;
}

int mqtt_connect(mqtt_client_t *c,
                 const char *clientid, const char *username, const char *password,
                 uint16_t keepalive)
{
    uint8_t connect_flags = 0x02;              /* Clean Session */
    uint16_t clen = (uint16_t)strlen(clientid);
    uint16_t ulen = (username != NULL) ? (uint16_t)strlen(username) : 0;
    uint16_t plen = (password != NULL) ? (uint16_t)strlen(password) : 0;

    if (ulen > 0)
    {
        connect_flags |= 0x80;
    }
    if (plen > 0)
    {
        connect_flags |= 0x40;
    }

    /* 预计算剩余长度（即固定字段 + 各字符串长度） */
    uint32_t remaining = 10U;                            /* MQTT名(6)+级别(1)+标志(1)+保活(2) */
    remaining += 2U + clen;                              /* ClientID */
    if (ulen > 0)
    {
        remaining += 2U + ulen;
    }
    if (plen > 0)
    {
        remaining += 2U + plen;
    }

    c->txlen = 0;
    tx_put_byte(c, 0x10);                      /* CONNECT 固定头 */
    tx_put_remaining(c, remaining);

    tx_put_str(c, "MQTT");                     /* 协议名 */
    tx_put_byte(c, 0x04);                      /* 协议级别 = 3.1.1 */
    tx_put_byte(c, connect_flags);
    tx_put_u16(c, keepalive);                  /* Keep Alive */

    tx_put_str(c, clientid);                   /* Client ID（必填） */
    if (ulen > 0)
    {
        tx_put_str(c, username);
    }
    if (plen > 0)
    {
        tx_put_str(c, password);
    }

    if (tx_flush(c) <= 0)
    {
        return -1;                             /* 发送失败 */
    }

    /* 等待 CONNACK：固定头 0x20，剩余长度 2，数据 2 字节 */
    uint8_t hdr, rl;
    if (rx_byte(c, &hdr, 3000) != 1 || hdr != 0x20)
    {
        return -2;                             /* 首字节非 CONNACK */
    }
    if (rx_byte(c, &rl, 3000) != 1 || rl != 0x02)
    {
        return -3;                             /* 剩余长度异常 */
    }
    uint8_t conn_flags, ret_code;
    if (rx_byte(c, &conn_flags, 3000) != 1)
    {
        return -4;
    }
    if (rx_byte(c, &ret_code, 3000) != 1)
    {
        return -5;
    }
    (void)conn_flags;                          /* 本实现忽略会话标志 */
    return (ret_code == MQTT_CONN_ACCEPTED) ? 0 : (int)ret_code;
}

int mqtt_publish(mqtt_client_t *c, const char *topic,
                 const uint8_t *payload, uint16_t plen, uint8_t qos)
{
    if (qos > 1)
    {
        return -6;                             /* 不支持 QoS2 */
    }
    uint16_t topic_len = (uint16_t)strlen(topic);

    /* 预计算剩余长度：主题(2+len) + [标识符(2)] + 负载 */
    uint32_t remaining = 2U + topic_len;
    if (qos > 0)
    {
        remaining += 2U;
    }
    remaining += plen;

    c->txlen = 0;
    tx_put_byte(c, (uint8_t)(0x30 | ((qos & 0x03) << 1)));   /* PUBLISH 固定头 */
    tx_put_remaining(c, remaining);
    tx_put_str(c, topic);
    if (qos > 0)
    {
        tx_put_u16(c, c->next_pkt_id++);       /* QoS1 需报文标识符 */
    }
    /* payload */
    for (uint16_t i = 0; i < plen; i++)
    {
        tx_put_byte(c, payload[i]);
    }

    if (tx_flush(c) <= 0)
    {
        return -7;
    }
    return 0;
}

uint16_t mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos)
{
    if (qos > 1)
    {
        return 0;
    }
    uint16_t pkt_id = c->next_pkt_id++;
    uint16_t topic_len = (uint16_t)strlen(topic);

    /* 剩余长度：标识符(2) + 主题(2+len) + qos(1) */
    uint32_t remaining = 2U + 2U + topic_len + 1U;

    c->txlen = 0;
    tx_put_byte(c, 0x82);                      /* SUBSCRIBE 固定头 + QoS1 */
    tx_put_remaining(c, remaining);
    tx_put_u16(c, pkt_id);
    tx_put_str(c, topic);
    tx_put_byte(c, qos);                       /* requested QoS */

    if (tx_flush(c) <= 0)
    {
        return 0;
    }
    return pkt_id;
}

int mqtt_ping(mqtt_client_t *c)
{
    c->txlen = 0;
    tx_put_byte(c, 0xC0);                      /* PINGREQ */
    tx_put_byte(c, 0x00);
    /* 按头文件契约返回 0=成功（底层 transport_send 成功返回 1，此处归一化为 0）。
       之前直接返回 tx_flush() 导致成功时返回 1，调用方按“0=成功”判断时
       永远不更新保活时间戳，从而每轮循环都刷 PINGREQ 淹没平台。 */
    return (tx_flush(c) > 0) ? 0 : -1;
}

void mqtt_disconnect(mqtt_client_t *c)
{
    c->txlen = 0;
    tx_put_byte(c, 0xE0);                      /* DISCONNECT */
    tx_put_byte(c, 0x00);
    (void)tx_flush(c);
}

/* 读取并处理服务端 PUBLISH 报文（剩余长度已读入 remaining） */
static int handle_publish(mqtt_client_t *c, uint32_t remaining)
{
    /* 解析主题 */
    uint8_t th, tl;
    if (rx_byte(c, &th, 3000) != 1 || rx_byte(c, &tl, 3000) != 1)
    {
        return -8;
    }
    uint16_t topic_len = (uint16_t)((th << 8) | tl);
    if (topic_len >= MQTT_RX_BUF_SIZE)
    {
        return -9;                             /* 主题过长 */
    }
    /* 读取主题 → 存入 rxbuf 头部 */
    uint16_t got = 0;
    while (got < topic_len)
    {
        int r = rx_byte(c, &c->rxbuf[got], 3000);
        if (r != 1)
        {
            return -10;
        }
        got++;
    }
    if (got >= MQTT_RX_BUF_SIZE)
    {
        got = MQTT_RX_BUF_SIZE - 1;
    }
    c->rxbuf[got] = '\0';                      /* 主题以 '\0' 结尾 */

    /* 读取负载。无论负载多大都整条读完：存得下的进缓冲，存不下的直接丢弃，
       确保退出本函数后字节流恰好位于下一条报文的固定头上，避免解析错位。 */
    uint32_t remaining_payload = remaining - (2U + (uint32_t)topic_len);
    uint8_t  pub_pkt_id[2] = {0, 0};
    if (c->pending_qos > 0)
    {
        if (rx_byte(c, &pub_pkt_id[0], 3000) != 1 || rx_byte(c, &pub_pkt_id[1], 3000) != 1)
        {
            return -11;
        }
        remaining_payload -= 2;
    }

    uint16_t payload_cap = (uint16_t)(MQTT_RX_BUF_SIZE - got - 1);
    uint16_t payload_pos = got + 1;
    uint16_t read_so_far = 0;
    uint8_t  discard_buf = 0;
    for (uint32_t i = 0; i < remaining_payload; i++)
    {
        int r = rx_byte(c, &discard_buf, 3000);
        if (r != 1)
        {
            break;                       /* 流被中断，无法继续对齐 */
        }
        if (read_so_far < payload_cap)
        {
            c->rxbuf[payload_pos + read_so_far] = discard_buf;
            read_so_far++;
        }
        /* 超过缓冲部分无需保存，仅消费该字节 */
    }

    /* 回调上层 */
    if (c->on_message != NULL && payload_pos > 0)
    {
        c->on_message((const char *)c->rxbuf, &c->rxbuf[payload_pos], read_so_far);
    }

    /* QoS1：回复 PUBACK */
    if (c->pending_qos > 0)
    {
        c->txlen = 0;
        tx_put_byte(c, 0x40);                  /* PUBACK */
        tx_put_byte(c, 0x02);
        tx_put_byte(c, pub_pkt_id[0]);
        tx_put_byte(c, pub_pkt_id[1]);
        (void)tx_flush(c);
    }

    (void)topic_len;
    return MQTT_PKT_PUBLISH;
}

int mqtt_poll(mqtt_client_t *c, uint32_t timeout_ms)
{
    uint8_t hdr;
    /* 等待首字节 */
    if (rx_byte(c, &hdr, timeout_ms) != 1)
    {
        return (c->t.recv != NULL) ? 0 : -100;   /* 0 = 超时无数据 */
    }

    uint8_t type = (uint8_t)(hdr & 0xF0);

    /* PINGRESP：固定头 0xD0 00 */
    if (type == MQTT_PKT_PINGRESP)
    {
        uint8_t rl;
        (void)rx_byte(c, &rl, 3000);
        return MQTT_PKT_PINGRESP;
    }

    /* PUBACK：固定头 0x40 02 xx xx */
    if (type == MQTT_PKT_PUBACK)
    {
        uint8_t rl;
        (void)rx_byte(c, &rl, 3000);
        for (uint32_t i = 0; i < rl; i++)
        {
            uint8_t b;
            if (rx_byte(c, &b, 3000) != 1)
            {
                break;
            }
        }
        return MQTT_PKT_PUBACK;
    }

    uint32_t remaining = 0;
    if (rx_remaining_len(c, &remaining) != 0)
    {
        return -12;
    }

    if (type == MQTT_PKT_CONNACK)
    {
        /* 由 mqtt_connect 处理，此处仅丢弃 */
        for (uint32_t i = 0; i < remaining; i++)
        {
            uint8_t b;
            if (rx_byte(c, &b, 3000) != 1)
            {
                break;
            }
        }
        return MQTT_PKT_CONNACK;
    }
    else if (type == MQTT_PKT_SUBACK)
    {
        for (uint32_t i = 0; i < remaining; i++)
        {
            uint8_t b;
            if (rx_byte(c, &b, 3000) != 1)
            {
                break;
            }
        }
        return MQTT_PKT_SUBACK;
    }
    else if (type == MQTT_PKT_PUBLISH)
    {
        /* QoS 从固定头 bit1：0x30|(qos<<1) */
        c->pending_qos = (uint8_t)((hdr >> 1) & 0x03);
        if (c->pending_qos > 2)
        {
            return -13;
        }
        return handle_publish(c, remaining);
    }
    else
    {
        /* 未知类型：丢弃剩余字节 */
        for (uint32_t i = 0; i < remaining; i++)
        {
            uint8_t b;
            if (rx_byte(c, &b, 3000) != 1)
            {
                break;
            }
        }
        return MQTT_PKT_UNKNOWN;
    }
}