/**
 ******************************************************************************
 * @file    debug.h
 * @brief   调试串口模块（USART1，连接电脑上位机）：
 *          - 格式化日志输出（类似 printf）；
 *          - 接收电脑端指令并解析成 “命令 + 参数”；
 *          - 通过回调把命令交给应用层执行（避免与 app 层产生循环依赖）。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 调试行缓冲大小 */
#define DEBUG_LINE_MAX   128U

/**
 * 调试命令回调原型。
 * @param cmd  命令名（不含空格，如 "help"、"wifi"）
 * @param arg  参数串（可能为空字符串 ""）
 * @return true 表示命令已识别并处理；false 表示未知命令
 */
typedef bool (*debug_command_cb_t)(const char *cmd, const char *arg);

/** 注册调试命令回调（应用层调用一次），并打印模块启动信息 */
void debug_init(debug_command_cb_t cb);

/** 格式化日志输出到调试串口（阻塞发送） */
void debug_printf(const char *fmt, ...);

/** 输出一行（自动补 \r\n） */
void debug_log(const char *line);

/**
 * 字幕轮询处理函数，需在主循环中周期性调用：
 * 从调试环形缓冲中逐字节提取完整的一行，调用注册的命令回调执行；
 * 空闲行（如回车）忽略。
 */
void debug_process(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEBUG_H */