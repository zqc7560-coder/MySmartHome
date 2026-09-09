/**
 ******************************************************************************
 * @file    usart_irq.h
 * @brief   串口中断服务程序模块（位于 User 目录）。
 *
 * 职责：
 *   - 定义 USART1 / USART2 的中断服务程序（对应中断向量表符号）；
 *   - 单字节接收中断处理：把接收到的字节经 HAL 回调写入对应环形缓冲；
 *   - 统一提供 usart_irq_rx_start() 使两条串口进入连续接收模式；
 *   - 错误处理：HAL_UART_ErrorCallback 中做溢出复位、重新挂起接收。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __USART_IRQ_H
#define __USART_IRQ_H

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 使能 USART1 / USART2 单字节接收中断（在 uart_init_all() 之后调用） */
HAL_StatusTypeDef usart_irq_rx_start(void);

/** 重新挂起 USART2(ESP8266) 的单字节接收中断（波特率重设后需重新调用） */
HAL_StatusTypeDef usart_esp_rx_rearm(void);

/** 供调试命令读取 USART1 接收缓冲对象的指针（一般不用，保留接口） */
void *usart_irq_debug_get_priv(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_IRQ_H */