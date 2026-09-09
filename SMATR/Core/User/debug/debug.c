/**
 ******************************************************************************
 * @file    debug.c
 * @brief   调试串口模块的实现。
 ******************************************************************************
 */
#include "debug/debug.h"
#include "uart/uart_config.h"
#include "common/ringbuf.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* 本地状态                                                                    */
/* ------------------------------------------------------------------------- */
static debug_command_cb_t s_cmd_cb = NULL;   /* 应用层注册的命令回调 */
static char s_line[DEBUG_LINE_MAX];          /* 行缓冲 */
static uint16_t s_line_len = 0;              /* 当前已缓冲字节数 */

static char s_printf_buf[192];               /* 日志格式化缓冲 */

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
void debug_init(debug_command_cb_t cb)
{
    s_cmd_cb = cb;
    /* 清空残留数据 */
    ringbuf_flush(&g_rb_debug_rx);
    s_line_len = 0;

    debug_log("");
    debug_log("==========================================");
    debug_log("  MySmartHome Debug Console  (USART1)");
    debug_log("  输入 help 查看可用调试指令");
    debug_log("==========================================");
}

void debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* vsnprintf 输出到本地缓冲，自带 '\0' 结尾 */
    int n = vsnprintf(s_printf_buf, sizeof(s_printf_buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return;          /* 格式化出错 */
    }
    if ((uint32_t)n > sizeof(s_printf_buf))
    {
        n = (int)sizeof(s_printf_buf);   /* 超长截断 */
    }
    uart_send(&huart_debug, (const uint8_t *)s_printf_buf, (uint16_t)n);
}

void debug_log(const char *line)
{
    (void)uart_send(&huart_debug, (const uint8_t *)line, (uint16_t)strlen(line));
    (void)uart_send(&huart_debug, (const uint8_t *)"\r\n", 2);
}

/* ------------------------------------------------------------------------- */
/* 指令解析：把 “cmd arg” 拆分为命令与参数                                    */
/* ------------------------------------------------------------------------- */
static void execute_command(char *line)
{
    if (s_cmd_cb == NULL)
    {
        return;
    }
    /* 去掉行尾 \r\n */
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
    {
        line[n - 1] = '\0';
        n--;
    }
    if (n == 0)
    {
        return;   /* 空行忽略 */
    }

    char *cmd = line;
    char *arg = "";
    /* 定位第一个空格作为命令与参数的分隔 */
    char *sp = strchr(line, ' ');
    if (sp != NULL)
    {
        *sp = '\0';
        arg = sp + 1;
    }

    if (!s_cmd_cb(cmd, arg))
    {
        debug_printf("[DEBUG] 未知指令: '%s'（输入 help 查看帮助）\r\n", cmd);
    }
}

/* ------------------------------------------------------------------------- */
/* 主循环轮询：从串口缓冲提取完整行并执行指令                                */
/* ------------------------------------------------------------------------- */
void debug_process(void)
{
    int16_t b;
    while ((b = ringbuf_read_byte(&g_rb_debug_rx)) >= 0)
    {
        uint8_t ch = (uint8_t)b;

        /* 行结束符：\n 或 \r */
        if (ch == '\n' || ch == '\r')
        {
            if (s_line_len > 0)
            {
                s_line[s_line_len] = '\0';
                execute_command(s_line);
                s_line_len = 0;
            }
            /* 忽略空的 \r\n */
            continue;
        }

        /* 退格删除（可选舒适性功能） */
        if ((ch == 0x08 || ch == 0x7F) && s_line_len > 0)
        {
            s_line_len--;
            continue;
        }

        if (s_line_len < (DEBUG_LINE_MAX - 1))
        {
            s_line[s_line_len++] = (char)ch;
        }
        else
        {
            /* 超长行：直接丢弃本行，防止缓冲溢出 */
            s_line_len = 0;
        }
    }
}