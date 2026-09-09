/**
 ******************************************************************************
 * @file    onenet.c
 * @brief   OneNet 云平台接入模块的实现。
 ******************************************************************************
 */
#include "onenet/onenet.h"
#include "esp8266/esp8266.h"
#include "common/delay.h"
#include "debug/debug.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* 本地状态                                                                    */
/* ------------------------------------------------------------------------- */
static mqtt_client_t     s_mqtt;            /* MQTT 客户端对象 */
static bool              s_connected = false;
static onenet_message_cb s_msg_cb = NULL;
static uint32_t          s_last_keepalive = 0;
static uint32_t          s_last_platform_rx  = 0;  /* 最近一次收到平台报文的时刻(用于活性自检) */
static uint16_t          s_keepalive_sec = ONENET_KEEPALIVE;

/* 运行时 WiFi 参数（初值取宏，可用 onenet_set_wifi 修改） */
static char s_wifi_ssid[32]  = ONENET_WIFI_SSID;
static char s_wifi_pass[32]  = ONENET_WIFI_PASS;

/* JSON 数据点缓冲 */
static char s_json[192];

/* ------------------------------------------------------------------------- */
/* 内部工具：手动格式化浮点数为固定小数位（避免依赖 newlib 的 %f 支持）       */
/* ------------------------------------------------------------------------- */
static int format_float(char *out, float value, int decimals)
{
    char *p = out;
    long ipart;
    long fpart;

    /* 负数取负号 */
    if (value < 0.0f)
    {
        *p++ = '-';
        value = -value;
    }
    /* 计算小数位因子 */
    long factor = 1;
    for (int i = 0; i < decimals; i++)
    {
        factor *= 10;
    }
    ipart = (long)value;
    fpart = (long)((value - (float)ipart) * (float)factor);
    if (fpart >= factor)
    {
        /* 四舍五入进位到整数部分 */
        ipart += 1;
        fpart = 0;
    }
    /* 写整数部分 */
    char istr[12];
    char *ip = istr + sizeof(istr) - 1;
    *ip = '\0';
    long t = ipart;
    if (t == 0)
    {
        *--ip = '0';
    }
    while (t > 0)
    {
        *--ip = (char)('0' + (t % 10));
        t /= 10;
    }
    while (*ip != '\0')
    {
        *p++ = *ip;
        ip++;
    }
    /* 写小数部分（若有小数位） */
    if (decimals > 0)
    {
        *p++ = '.';
        char fbuf[12];
        char *fp = fbuf + sizeof(fbuf) - 1;
        *fp = '\0';
        t = fpart;
        for (int i = 0; i < decimals; i++)
        {
            if (t == 0)
            {
                *--fp = '0';
            }
            else
            {
                *--fp = (char)('0' + (t % 10));
                t /= 10;
            }
        }
        while (*fp != '\0')
        {
            *p++ = *fp;
            fp++;
        }
    }
    *p = '\0';
    return (int)(p - out);
}

/* ------------------------------------------------------------------------- */
/* MQTT 传输层回调：把 MQTT 协议字节流接到 ESP8266 透传通道上                  */
/* ------------------------------------------------------------------------- */
static int transport_send(const uint8_t *data, uint16_t len)
{
    return (esp_trans_send(data, len) == len) ? 1 : 0;
}

static int transport_recv(uint8_t *data, uint16_t max_len, uint32_t timeout_ms)
{
    return (int)esp_trans_recv(data, max_len, timeout_ms);
}

/* ------------------------------------------------------------------------- */
/* MQTT 消息到达回调（协议栈触发）                                            */
/* ------------------------------------------------------------------------- */
static void mqtt_on_message(const char *topic, const uint8_t *payload, uint16_t plen)
{
    /* 上报给应用层（可能用于解析 $creq/{device_id} 命令） */
    if (s_msg_cb != NULL)
    {
        s_msg_cb(topic, payload, plen);
    }
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
void onenet_init(onenet_message_cb cb)
{
    s_connected = false;
    s_last_keepalive = 0;
    onenet_set_message_cb(cb);
}

void onenet_set_message_cb(onenet_message_cb cb)
{
    s_msg_cb = cb;
}

void onenet_set_wifi(const char *ssid, const char *pass)
{
    (void)snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", (ssid != NULL) ? ssid : "");
    (void)snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", (pass != NULL) ? pass : "");
}

int onenet_connect(void)
{
    /* 0) 先强制退回指令模式：若上次连接在进入 TCP 透传后失败，
           ESP 会残留在透传模式，此时后续所有 AT 指令都会被当作透传数据
           而非指令解析，导致一直返回 ONENET_ERR_ESP(-1)。此调用在普通
           指令模式下也无副作用。 */
    esp_force_command_mode();
    delay_ms(200);

    /* 1) 自动扫描并锁定 ESP8266 波特率（应对模块波特率未知/被改过的情况） */
    uint32_t baud = 0;
    if (!esp_autobaud(&baud))
    {
        debug_log("[ONENET] 未匹配到 ESP8266 波特率（请检查接线与模块 AT 固件）");
        return ONENET_ERR_ESP;
    }
    debug_printf("[ONENET] 识别波特率=%lu, 继续初始化\r\n", (unsigned long)baud);

    /* 2) 关闭回显（ATE0）。
          注意：此处不做 AT+RST 软复位——模块重启后波特率会回到它 Flash 里
               保存的默认值，丢掉刚自动同步到的波特率导致再次失步。自动识别已
               能建立稳定通讯，直接关回显即可。 */
    if (!esp_at_init(2000))
    {
        debug_log("[ONENET] 失败: ATE0 未应答");
        return ONENET_ERR_ESP;
    }

    /* 3) 进入 Station 模式并连接 WiFi */
    if (!esp_set_mode(ESP_MODE_STATION, 2000))
    {
        debug_log("[ONENET] 失败: 设置 Station 模式失败");
        return ONENET_ERR_ESP;
    }
    delay_ms(300);
    if (!esp_join_ap(s_wifi_ssid, s_wifi_pass, ESP_JOIN_TIMEOUT))
    {
        debug_log("[ONENET] 失败: 连接 WiFi 失败");
        return ONENET_ERR_JOIN_AP;
    }

    /* 4) 建立与 OneNet Broker 的 TCP 透传连接 */
    if (!esp_open_transparent(ONENET_BROKER_HOST, ONENET_BROKER_PORT, 10000))
    {
        debug_log("[ONENET] 失败: TCP 透传建立失败");
        return ONENET_ERR_TCP;
    }

    /* 5) 组装 MQTT 传输层并初始化客户端 */
    mqtt_transport_t t;
    t.send = transport_send;
    t.recv = transport_recv;
    mqtt_client_init(&s_mqtt, &t, mqtt_on_message);

    /* 5) 发送 MQTT CONNECT 并等待 CONNACK */
    debug_printf("[ONENET] MQTT凭据: client_id=\"%s\" user=\"%s\" pass=\"%s\"\r\n",
                 ONENET_DEVICE_ID, ONENET_PRODUCT_ID, ONENET_API_KEY);
    int rc = mqtt_connect(&s_mqtt,
                          ONENET_DEVICE_ID,
                          ONENET_PRODUCT_ID,
                          ONENET_API_KEY,
                          ONENET_KEEPALIVE);
    if (rc != 0)
    {
        /* 打印原始错误码便于定位：-1=发送失败 -2=首字节非CONNACK -3=剩余长度异常
           -4/-5=应答未读全(超时)；>0 为 CONNACK 拒连码 */
        debug_printf("[ONENET] MQTT CONNECT 结果=%d\r\n", rc);
        /* 失败后立即断开 TCP 透传，避免残留连接导致下一次协议解析错乱 */
        esp_close_transparent();
        return (rc < 0) ? ONENET_ERR_MQTT_CONN : (ONENET_ERR_MQTT_CONN + 1000 + rc);
    }

    s_connected = true;
    s_last_keepalive = now_ms();
    s_last_platform_rx = now_ms();
    return ONENET_OK;
}

void onenet_disconnect(void)
{
    if (s_connected)
    {
        mqtt_disconnect(&s_mqtt);
        s_connected = false;
    }
    esp_close_transparent();
}

bool onenet_is_connected(void)
{
    return s_connected;
}

int onenet_publish_raw(const char *topic, const uint8_t *payload, uint16_t plen, uint8_t qos)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    int rc = mqtt_publish(&s_mqtt, topic, payload, plen, qos);
    return (rc == 0) ? ONENET_OK : ONENET_ERR_ESP;
}

int onenet_publish_dp(const char *dsid, float value)
{
    /* $dp 主题上报单个数值数据点，JSON 格式；浮点用手动格式化避免依赖 %f */
    char val_buf[24];
    (void)format_float(val_buf, value, 2);

    int m = snprintf(s_json, sizeof(s_json),
                     "{\"datastreams\":[{\"id\":\"%s\",\"datapoints\":[{\"value\":%s}]}]}",
                     (dsid != NULL) ? dsid : "value",
                     val_buf);
    if (m <= 0)
    {
        return ONENET_ERR_ESP;
    }
    return onenet_publish_raw("$dp", (const uint8_t *)s_json, (uint16_t)m, 0);
}

int onenet_publish_dp_str(const char *dsid, const char *str)
{
    int m = snprintf(s_json, sizeof(s_json),
                     "{\"datastreams\":[{\"id\":\"%s\",\"datapoints\":[{\"value\":\"%s\"}]}]}",
                     (dsid != NULL) ? dsid : "value",
                     (str != NULL) ? str : "");
    if (m <= 0)
    {
        return ONENET_ERR_ESP;
    }
    return onenet_publish_raw("$dp", (const uint8_t *)s_json, (uint16_t)m, 0);
}

/* ------------------------------------------------------------------------- */
/* 诊断用：以 QoS1 发布并等待平台回 PUBACK，验证数据是否真正到达 OneNet。      */
/* 返回 0=收到PUBACK(数据已到达)；<0=发送失败； >0(1)=发布后未等到PUBACK。    */
/* ------------------------------------------------------------------------- */
int onenet_publish_wait_ack(const char *dsid, float value)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    char val_buf[24];
    (void)format_float(val_buf, value, 2);
    int m = snprintf(s_json, sizeof(s_json),
                     "{\"datastreams\":[{\"id\":\"%s\",\"datapoints\":[{\"value\":%s}]}]}",
                     (dsid != NULL) ? dsid : "value", val_buf);
    if (m <= 0)
    {
        return ONENET_ERR_ESP;
    }
    int rc = mqtt_publish(&s_mqtt, "$dp", (const uint8_t *)s_json, (uint16_t)m, 1); /* QoS1 */
    if (rc != 0)
    {
        return rc;
    }
    /* 发包后等待 PUBACK（最多 4s），同时顺带处理可能夹杂的命令报文 */
    uint32_t deadline = now_ms() + 4000U;
    do
    {
        int pr = mqtt_poll(&s_mqtt, 500);
        if (pr == MQTT_PKT_PUBACK)
        {
            return 0;                          /* 平台确认收到 */
        }
        if (pr < 0)
        {
            return pr;                         /* 协议层错误 */
        }
        /* PUBLISH 命令已在 mqtt_poll 内派发回调，继续等 PUBACK */
    } while (!(now_ms() >= deadline));
    return 1;                                  /* 超时未收到 PUBACK */
}

int onenet_subscribe_cmd(void)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    uint16_t id = mqtt_subscribe(&s_mqtt, ONENET_CMD_TOPIC, 0);
    return (id > 0) ? ONENET_OK : ONENET_ERR_ESP;
}

/* ------------------------------------------------------------------------- */
/* 新版物模型：上报单个属性到 thing/property/post                              */
/* OneJSON 负载：{"id":"..","version":"1.0","params":{<标识符>:{"value":x}}}  */
/* ------------------------------------------------------------------------- */
int onenet_report_property(const char *idstr, float value)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    char val_buf[24];
    (void)format_float(val_buf, value, 2);
    static uint32_t s_prop_id = 100;                 /* 消息id（平台要求≤13位数字串） */
    int m = snprintf(s_json, sizeof(s_json),
                     "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"%s\":{\"value\":%s}}}",
                     (unsigned long)s_prop_id,
                     (idstr != NULL) ? idstr : "value",
                     val_buf);
    if (m <= 0)
    {
        return ONENET_ERR_ESP;
    }
    s_prop_id++;
    return onenet_publish_raw(ONENET_PROP_POST_TOPIC, (const uint8_t *)s_json, (uint16_t)m, 0);
}

/* ------------------------------------------------------------------------- */
/* 新版物模型：订阅平台属性设置主题 thing/property/set                         */
/* ------------------------------------------------------------------------- */
int onenet_subscribe_property_set(void)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    uint16_t id = mqtt_subscribe(&s_mqtt, ONENET_PROP_SET_TOPIC, 0);
    return (id > 0) ? ONENET_OK : ONENET_ERR_ESP;
}

/* ------------------------------------------------------------------------- */
/* 新版物模型：订阅平台属性获取主题 thing/property/get                         */
/* 平台获取属性时若设备不回 get_reply 会报“设备响应超时(10414)”并显示 undefined， */
/* 因此必须订阅并在 get 到来时回传当前属性值。                                  */
/* ------------------------------------------------------------------------- */
int onenet_subscribe_property_get(void)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    uint16_t id = mqtt_subscribe(&s_mqtt, ONENET_PROP_GET_TOPIC, 0);
    return (id > 0) ? ONENET_OK : ONENET_ERR_ESP;
}

/* ------------------------------------------------------------------------- */
/* 新版物模型：回传属性获取应答 thing/property/get_reply                       */
/* 应答格式：{"id":"..","code":200,"msg":"ok","data":{<标识符>:值,...}}        */
/* ------------------------------------------------------------------------- */
int onenet_thing_property_get_reply(const char *pkt_id, float temp, int led)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    char tb[24];
    (void)format_float(tb, temp, 2);
    int m = snprintf(s_json, sizeof(s_json),
                     "{\"id\":\"%s\",\"code\":200,\"msg\":\"ok\",\"data\":{\""
                     ONENET_TEMP_PROP_ID "\":%s,\"" ONENET_LED_PROP_ID "\":%d}}",
                     (pkt_id != NULL) ? pkt_id : "0",
                     tb,
                     led ? 1 : 0);
    if (m <= 0)
    {
        return ONENET_ERR_ESP;
    }
    return onenet_publish_raw(ONENET_PROP_GET_REPLY, (const uint8_t *)s_json, (uint16_t)m, 0);
}

/* ------------------------------------------------------------------------- */
/* 新版物模型：对属性设置返回应答 thing/property/set_reply                     */
/* ------------------------------------------------------------------------- */
int onenet_reply_property_set(const char *pkt_id, int code, const char *msg)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    int m = snprintf(s_json, sizeof(s_json),
                     "{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}",
                     (pkt_id != NULL) ? pkt_id : "0", code,
                     (msg != NULL) ? msg : "ok");
    if (m <= 0)
    {
        return ONENET_ERR_ESP;
    }
    return onenet_publish_raw(ONENET_PROP_SET_REPLY, (const uint8_t *)s_json, (uint16_t)m, 0);
}

/* ------------------------------------------------------------------------- */
/* 连接活性探针：发送 PINGREQ 并等待任何回复（PINGRESP 等）。                 */
/* 返回 1=收到回复(连接是活的)；0=无回复(连接真断)；<0 未连接/发送失败。       */
/* ------------------------------------------------------------------------- */
int onenet_probe(void)
{
    if (!s_connected)
    {
        return ONENET_ERR_NOT_CONNECTED;
    }
    if (mqtt_ping(&s_mqtt) != 0)
    {
        return -1;
    }
    uint32_t deadline = now_ms() + 3000U;
    do
    {
        int pr = mqtt_poll(&s_mqtt, 1000);
        if (pr > 0)
        {
            return 1;                          /* 有任何报文回来都说明链路活着 */
        }
    } while (!(now_ms() >= deadline));
    return 0;                                  /* 超时无回复：链路已断 */
}

/* ------------------------------------------------------------------------- */
/* 活性自检：返回距最近一次收到平台报文已过的毫秒数。                         */
/* 未连接或无法计时时返回 0xFFFFFFFF。                                         */
/* 配套 app 层看门狗：当该值超过阈值仍收不到任何回包，即判定会话失效并重连。   */
/* ------------------------------------------------------------------------- */
uint32_t onenet_platform_rx_age_ms(void)
{
    if (!s_connected)
    {
        return 0xFFFFFFFFU;
    }
    return (now_ms() - s_last_platform_rx);
}

void onenet_yield(void)
{
    if (!s_connected)
    {
        return;
    }

    /* 接收并处理云端下发报文（含 $creq 命令、SUBACK、PINGRESP 等），短超时保持响应及时 */
    int pr = mqtt_poll(&s_mqtt, 10);
    if (pr > 0)
    {
        /* 收到平台任一报文 → 会话仍被平台持有，刷新活性时间戳 */
        s_last_platform_rx = now_ms();
        /* 诊断：打印平台回包类型，用于判断平台到底发了什么、何时停止 */
        static uint8_t s_last_pkt = 0xFF;
        if (pr != s_last_pkt)
        {
            s_last_pkt = (uint8_t)pr;
            debug_printf("[ONENET] 收到平台报文 type=0x%02X\r\n", (uint8_t)pr);
        }
    }

    /* 心跳保活：超过 keepalive/2 秒未活动则发送 PINGREQ。
       注意：mqtt_ping 成功返回 0，必须用 ==0 判断“已发出”，
            用 >0 会导致成功时不更新保活时间戳而反复刷 PINGREQ。 */
    if ((now_ms() - s_last_keepalive) >= (s_keepalive_sec * 500U))
    {
        if (mqtt_ping(&s_mqtt) == 0)
        {
            s_last_keepalive = now_ms();
        }
    }
}