/**
 ******************************************************************************
 * @file    app.c
 * @brief   应用编排层实现。
 ******************************************************************************
 */
#include "app/app.h"
#include "debug/debug.h"
#include "uart/uart_config.h"
#include "port/usart_irq.h"
#include "esp8266/esp8266.h"
#include "onenet/onenet.h"
#include "led/led.h"
#include "common/ringbuf.h"
#include "common/delay.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* 应用状态与常量                                                              */
/* ------------------------------------------------------------------------- */
typedef enum
{
    APP_STATE_IDLE = 0,
    APP_STATE_CONNECTING,
    APP_STATE_CONNECTED
} app_state_t;

#define APP_DEMO_PERIOD_MS           5000U   /* 演示数据上报周期 */
#define APP_RETRY_PERIOD_MS          5000U   /* 连接失败重试间隔 */
#define APP_PLATFORM_STALE_MS        30000U  /* 平台回包看门狗：30s 无任何回包即重连
                                                (keepalive=30s, PINGREQ 每15s, 余量1倍) */

static app_state_t s_state     = APP_STATE_IDLE;
static bool        s_subscribed = false;
static uint32_t    s_demo_timer = 0;
static uint32_t    s_retry_timer = 0;
static uint32_t    s_last_activity = 0;
static float       s_demo_temp   = 26.0f;   /* 最近一次有效温度 */
static uint16_t    s_demo_hum    = 500;     /* 最近一次有效湿度(0.1%RH，500=50.0%RH) */

/* ------------------------------------------------------------------------- */
/* 静态函数声明                                                              */
/* ------------------------------------------------------------------------- */
static bool app_cmd_handler(const char *cmd, const char *arg);
static void app_log_status(void);
static void onenet_message_cb_impl(const char *topic, const uint8_t *payload, uint16_t plen);
static void app_start_connect(void);

/* ------------------------------------------------------------------------- */
/* 云端命令回调：收到 $creq/{device_id} 下发指令时执行                          */
/* 支持的 LED 指令：on/off/toggle、1/0。                                       */
/* ------------------------------------------------------------------------- */
static void led_apply_command(const char *cmd)
{
    if (cmd == NULL)
    {
        return;
    }
    /* 跳过首部空白 */
    while (*cmd == ' ' || *cmd == '\r' || *cmd == '\n' || *cmd == '\t')
    {
        cmd++;
    }
    if (strcmp(cmd, "on")   == 0 || strcmp(cmd, "1")       == 0 ||
        strcmp(cmd, "led_on") == 0)
    {
        led_on();
    }
    else if (strcmp(cmd, "off")    == 0 || strcmp(cmd, "0")       == 0 ||
             strcmp(cmd, "led_off") == 0)
    {
        led_off();
    }
    else if (strcmp(cmd, "toggle") == 0)
    {
        led_toggle();
    }
    else
    {
        debug_printf("[LED] 未知命令: %s\r\n", cmd);
        return;
    }
    debug_printf("[LED] 云端控制: LED=%s\r\n", led_get() ? "亮" : "灭");
    /* 把最新状态回传平台，保证平台端能实时看到亮灭 */
    (void)led_publish_state();
}

/* ------------------------------------------------------------------------- */
/* 内部：从 OneJSON 负载里提取 "id" 字段值（用于应答回显）                     */
/* ------------------------------------------------------------------------- */
static void app_json_get_id(const char *tmp, char *out, uint16_t outsz)
{
    out[0] = '\0';
    const char *p = strstr(tmp, "\"id\"");
    if (p != NULL)
    {
        p = strchr(p, ':');
        if (p != NULL)
        {
            p++;
            while (*p == ' ' || *p == '"')
            {
                p++;
            }
            uint16_t i = 0;
            while (p[i] != '\0' && p[i] != '"' && i < (outsz - 1))
            {
                out[i] = p[i];
                i++;
            }
            out[i] = '\0';
        }
    }
}

/* ------------------------------------------------------------------------- */
/* 云端命令回调：定义物模型属性设置 thing/property/set,实现云端控制 LED。        */
/* 负载示例：{"id":"123","version":"1.0","params":{"led":1}}                    */
/* ------------------------------------------------------------------------- */
static void onenet_property_set_handle(const char *payload, uint16_t plen)
{
    char tmp[128];
    uint16_t n = (plen < (sizeof(tmp) - 1)) ? plen : (uint16_t)(sizeof(tmp) - 1);
    memcpy(tmp, payload, n);
    tmp[n] = '\0';

    /* 诊断：打印平台 set 的真实原文，核对应答 id 是否与请求中的 id 一致 */
    debug_printf("[ONENET] set 原文: %s\r\n", tmp);

    /* 提取响应用 id（OneNet 的请求 id 可能较长，缓冲给足避免截断导致应答无法关联） */
    char pkt_id[64] = "0";
    app_json_get_id(tmp, pkt_id, sizeof(pkt_id));

    /* 查找 LED 属性标识符并按其值控制 */
    bool handled = false;
    const char *q = strstr(tmp, "\"" ONENET_LED_PROP_ID "\"");
    if (q != NULL)
    {
        q = strchr(q, ':');
        if (q != NULL)
        {
            q++;
            while (*q == ' ' || *q == '\t')
            {
                q++;
            }
            if (strncmp(q, "true", 4) == 0 || q[0] == '1')
            {
                led_on();
                handled = true;
            }
            else if (strncmp(q, "false", 5) == 0 || q[0] == '0')
            {
                led_off();
                handled = true;
            }
            else if (strncmp(q, "\"on\"", 4) == 0)
            {
                led_on();
                handled = true;
            }
            else if (strncmp(q, "\"off\"", 5) == 0)
            {
                led_off();
                handled = true;
            }
        }
    }

    /* 先回传应答，让平台端设置操作尽快成功（应答越早，越不容易超时） */
    int rc = onenet_reply_property_set(pkt_id, 200, "ok", handled ? (led_get() ? 1 : 0) : -1);
    debug_printf("[ONENET] set_reply id=%s rc=%d\r\n", pkt_id, rc);

    if (handled)
    {
        debug_printf("[LED] 云端物模型设置: LED=%s (id=%s)\r\n", led_get() ? "亮" : "灭", pkt_id);
        (void)led_publish_state();
    }
    else
    {
        debug_printf("[ONENET] 属性设置未识别或非LED id=%s: %s\r\n", pkt_id, tmp);
    }
}

/* ------------------------------------------------------------------------- */
/* 云端命令回调：处理物模型属性获取 thing/property/get，回传当前属性值。         */
/* ------------------------------------------------------------------------- */
static void onenet_property_get_handle(const char *payload, uint16_t plen)
{
    char tmp[128];
    uint16_t n = (plen < (sizeof(tmp) - 1)) ? plen : (uint16_t)(sizeof(tmp) - 1);
    memcpy(tmp, payload, n);
    tmp[n] = '\0';

    char pkt_id[64] = "0";
    app_json_get_id(tmp, pkt_id, sizeof(pkt_id));

    debug_log("[ONENET] 收到属性获取请求，回传当前属性");
    debug_printf("[ONENET] get id=%s\r\n", pkt_id);
    /* 把当前温度与 LED 状态回传给平台（get_reply） */
    (void)onenet_thing_property_get_reply(pkt_id, s_demo_temp, s_demo_hum, led_get() ? 1 : 0);
}

static void onenet_message_cb_impl(const char *topic, const uint8_t *payload, uint16_t plen)
{
    /* 诊断：打印每条下发消息的真实主题，用于核对 set/get 到底走哪个通道 */
    debug_printf("[ONENET] 下发 topic=%s\r\n", topic);
    (void)plen;
    (void)payload;
    /* $sys/.../thing/property/set 为物模型属性设置（云端控制） */
    if (strstr(topic, "/thing/property/set") != NULL)
    {
        onenet_property_set_handle((const char *)payload, plen);
        return;
    }
    /* $sys/.../thing/property/get 为物模型属性获取（平台读当前值） */
    if (strstr(topic, "/thing/property/get") != NULL)
    {
        onenet_property_get_handle((const char *)payload, plen);
        return;
    }

    char tmp[64];
    uint16_t n = (plen < (sizeof(tmp) - 1)) ? plen : (uint16_t)(sizeof(tmp) - 1);
    memcpy(tmp, payload, n);
    tmp[n] = '\0';
    debug_printf("[ONENET] 收到主题 '%s' 命令: %s\r\n", topic, tmp);

    /* $creq/{device_id} 旧命令通道：解析并控制 LED */
    if (strstr(topic, "$creq/") != NULL)
    {
        led_apply_command(tmp);
    }
}

/* ------------------------------------------------------------------------- */
/* 状态切换辅助                                                                */
/* ------------------------------------------------------------------------- */
static void app_start_connect(void)
{
    s_state = APP_STATE_CONNECTING;
    s_subscribed = false;
    s_last_activity = now_ms();
    debug_log("[APP] 开始连接 OneNet ...");
}

/* ------------------------------------------------------------------------- */
/* 状态打印                                                                    */
/* ------------------------------------------------------------------------- */
static void app_log_status(void)
{
    debug_printf("[APP] 状态=%s  连接=%s  ESP透传=%s\r\n",
                 (s_state == APP_STATE_IDLE)      ? "IDLE" :
                 (s_state == APP_STATE_CONNECTING)? "CONNECTING" : "CONNECTED",
                 onenet_is_connected() ? "已连接" : "未连接",
                 esp_is_transparent() ? "已使能" : "未使能");
}

/* ------------------------------------------------------------------------- */
/* 调试命令处理                                                                */
/* ------------------------------------------------------------------------- */
static bool app_cmd_handler(const char *cmd, const char *arg)
{
    if (strcmp(cmd, "help") == 0)
    {
        debug_log("可用指令:");
        debug_log("  help                    显示帮助");
        debug_log("  status                  显示系统/连接状态");
        debug_log("  connect                 手动连接 OneNet");
        debug_log("  disconnect              手动断开连接");
        debug_log("  wifi <ssid> <pass>      修改 WiFi 并重连");
        debug_log("  publish [value]         上报一个数值(demo, 默认温度)");
        debug_log("  ping                    发送 MQTT 心跳");
        debug_log("  led on|off|toggle       本地控制 LED");
        debug_log("  test                    运行内置自测");
        debug_log("  reset                   ESP8266 软件复位");
        return true;
    }
    else if (strcmp(cmd, "status") == 0)
    {
        app_log_status();
        return true;
    }
    else if (strcmp(cmd, "connect") == 0)
    {
        app_start_connect();
        return true;
    }
    else if (strcmp(cmd, "disconnect") == 0)
    {
        onenet_disconnect();
        s_state = APP_STATE_IDLE;
        app_log_status();
        return true;
    }
    else if (strcmp(cmd, "wifi") == 0)
    {
        /* 参数格式: wifi <ssid> <pass>，用第一个空格切命令名与参数，ssid 与 pass 之间再切一刀 */
        char ssid[32] = {0}, pass[32] = {0};
        if (strlen(arg) == 0)
        {
            debug_log("[APP] 用法: wifi <ssid> <pass>");
            return true;
        }
        const char *sp = strchr(arg, ' ');
        if (sp != NULL)
        {
            (void)snprintf(ssid, sizeof(ssid), "%.*s", (int)(sp - arg), arg);
            (void)snprintf(pass, sizeof(pass), "%s", sp + 1);
        }
        else
        {
            (void)snprintf(ssid, sizeof(ssid), "%s", arg);
        }
        onenet_set_wifi(ssid, pass);
        debug_printf("[APP] WiFi 已更新: ssid=%s\r\n", ssid);
        onenet_disconnect();
        app_start_connect();
        return true;
    }
    else if (strcmp(cmd, "publish") == 0)
    {
        float v = s_demo_temp;
        if (strlen(arg) > 0)
        {
            v = (float)atof(arg);
        }
        int rc = onenet_publish_dp("temperature", v);
        debug_printf("[APP] 上报 temperature=%d, rc=%d\r\n", (int)v, rc);
        return true;
    }
    else if (strcmp(cmd, "ping") == 0)
    {
        int rc = onenet_is_connected() ? 0 : -1;
        if (rc == 0)
        {
            debug_log("[APP] 由 onenet_yield 负责保活心跳");
        }
        else
        {
            debug_log("[APP] 未连接，无法发送心跳");
        }
        return true;
    }
    else if (strcmp(cmd, "probe") == 0)
    {
        int r = onenet_probe();
        debug_printf("[APP] 连接活性探针: proberc=%d (%s)\r\n",
                     r, (r == 1) ? "链路活着" : (r == 0) ? "链路已断" : "未连接/发送失败");
        return true;
    }
    else if (strcmp(cmd, "led") == 0)
    {
        /* 本地控制 LED：led on / led off / led toggle */
        if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0)
        {
            led_on();
            debug_printf("[LED] 本地控制: LED=%s\r\n", led_get() ? "亮" : "灭");
            (void)led_publish_state();
        }
        else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0)
        {
            led_off();
            debug_printf("[LED] 本地控制: LED=%s\r\n", led_get() ? "亮" : "灭");
            (void)led_publish_state();
        }
        else if (strcmp(arg, "toggle") == 0)
        {
            led_toggle();
            debug_printf("[LED] 本地控制: LED=%s\r\n", led_get() ? "亮" : "灭");
            (void)led_publish_state();
        }
        else
        {
            debug_log("[APP] 用法: led on|off|toggle");
        }
        return true;
    }
    else if (strcmp(cmd, "test") == 0)
    {
        if (app_selftest())
        {
            debug_log("[APP] 自测: 通过 (PASS)");
        }
        else
        {
            debug_log("[APP] 自测: 失败 (FAIL)");
        }
        return true;
    }
    else if (strcmp(cmd, "reset") == 0)
    {
        esp_reset(3000);
        app_start_connect();
        return true;
    }
    return false;   /* 未知命令 */
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
void app_init(void)
{
    /* 1) 初始化两条 UART */
    (void)uart_init_all();

    /* 2) 挂起两条串口的接收中断 */
    (void)usart_irq_rx_start();

    /* 3) 初始化调试控制台并注册命令回调 */
    debug_init(app_cmd_handler);

    /* 3.5) 初始化 LED 灯 */
    led_init();

    /* 4) 初始化 OneNet（注册云端消息回调） */
    onenet_init(onenet_message_cb_impl);

    debug_log("[APP] 应用初始化完成，准备连接 OneNet ...");
    app_start_connect();
}

void app_loop(void)
{
    /* 处理电脑端调试指令 */
    debug_process();

    switch (s_state)
    {
        case APP_STATE_IDLE:
            /* 等待用户操作 */
            break;

        case APP_STATE_CONNECTING:
            /* 连接失败后按一定间隔重试，避免刷屏 */
            if ((now_ms() - s_retry_timer) >= APP_RETRY_PERIOD_MS)
            {
                s_retry_timer = now_ms();
                debug_log("[APP] 正在连接 OneNet 云平台 ...");
                int rc = onenet_connect();
                if (rc == ONENET_OK)
                {
                    debug_log("[APP] 连接成功！");
                    s_state = APP_STATE_CONNECTED;
                    s_last_activity = now_ms();
                    s_subscribed = false;
                }
                else
                {
                    debug_printf("[APP] 连接失败, rc=%d, 稍后重试 ...\r\n", rc);
                }
            }
            break;

        case APP_STATE_CONNECTED:
        {
            /* 周期性处理云端收发 + 心跳（建议高频调用，内部带短超时） */
            onenet_yield();

            /* 首次成功后订阅物模型设置与获取主题（不再订阅旧 $creq 命令主题，
               旧主题在新版物模型下可能被 ACL 判定越权而触发服务器断开连接） */
            if (!s_subscribed)
            {
                bool ok = onenet_subscribe_property_set() == ONENET_OK;
                ok = onenet_subscribe_property_get() == ONENET_OK && ok;
                if (ok)
                {
                    s_subscribed = true;
                    debug_log("[APP] 已订阅物模型设置与获取主题");
                }
            }

            /* 演示数据周期上报（用实时秒的 % 判断是否到点） */
            if ((now_ms() - s_demo_timer) >= APP_DEMO_PERIOD_MS)
            {
                s_demo_timer = now_ms();

                /* 模拟温度数据（不依赖传感器）：在 20~32℃ 之间缓慢随机游走 */
                int step = (rand() % 5) - 2;               /* -2 ~ +2 */
                s_demo_temp += (float)step;
                if (s_demo_temp > 32.0f)
                {
                    s_demo_temp = 32.0f;
                }
                if (s_demo_temp < 20.0f)
                {
                    s_demo_temp = 20.0f;
                }

                int rc = onenet_report_property(ONENET_TEMP_PROP_ID, s_demo_temp);
                debug_printf("[APP] 周期上报 EnvironmentTemperature=%d, rc=%d\r\n", (int)s_demo_temp, rc);
                if (rc != ONENET_OK)
                {
                    /* 发布失败(发送层面)视为链路异常，触发重连 */
                    debug_log("[APP] 上报失败，触发重连");
                    app_start_connect();
                    break;
                }
                /* 同步上报 LED 亮灭状态，让平台端始终能看到当前状态 */
                (void)led_publish_state();
                s_last_activity = now_ms();
            }

            /* 活性看门狗：以“是否还能收到平台回包”为准（PINGRESP 每 keepalive 应到）。
               若长时间收不到任何平台报文，说明会话已被平台断开，强制重新建立。 */
            uint32_t rx_age = onenet_platform_rx_age_ms();
            if (rx_age != 0xFFFFFFFFU && rx_age > APP_PLATFORM_STALE_MS)
            {
                debug_log("[ONENET] 平台长时间无回包(会话可能失效)，触发重连");
                onenet_disconnect();
                app_start_connect();
                break;
            }
            break;
        }

        default:
            s_state = APP_STATE_IDLE;
            break;
    }
}

int app_selftest(void)
{
    bool ok = true;

    /* 1) 环形缓冲单元自测 */
    if (!ringbuf_selftest())
    {
        debug_log("[TEST] 环形缓冲自测 失败");
        ok = false;
    }
    else
    {
        debug_log("[TEST] 环形缓冲自测 通过");
    }

    /* 2) 调试串口回环功能是否正常由上位机手动验证（此处打印一条测试标记） */
    debug_log("[TEST] 调试串口(UART1)工作正常");

    return ok ? 0 : -1;
}