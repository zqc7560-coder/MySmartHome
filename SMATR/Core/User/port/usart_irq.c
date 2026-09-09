/**
 ******************************************************************************
 * @file    usart_irq.c
 * @brief   串口中断服务程序模块的实现。
 *
 * 设计说明：
 *   - USART1（调试）与 USART2（ESP8266）均采用“单字节 + 重挂载”的方式：
 *     每接收到一字节触发中断，ISR 调用 HAL_UART_IRQHandler()，其内部回调
 *     HAL_UART_RxCpltCallback()，此时我们把该字节压入对应环形缓冲，并立刻
 *     重新调用 HAL_UART_Receive_IT() 等待下一个字节 —— 从而形成连续、实时、
 *     且无丢失的接收。环形缓冲由主循环消费，天然满足“ISR 只写 / 主程序只读”。
 ******************************************************************************
 */
#include "port/usart_irq.h"
#include "uart/uart_config.h"
#include "common/ringbuf.h"

/* ------------------------------------------------------------------------- */
/* 单字节接收缓存：HAL 把中断收到的字节写入这里，回调再转入环形缓冲         */
/* ------------------------------------------------------------------------- */
static volatile uint8_t s_rx1_byte;   /* USART1 调试串口当前中断字节 */
static volatile uint8_t s_rx2_byte;   /* USART2 ESP8266 串口当前中断字节 */

/* ------------------------------------------------------------------------- */
/* 中断向量：USART1 / USART2 全局中断                                       */
/* 这两个符号与 startup 启动文件的 NVIC 向量表对应，在此提供实现。           */
/* ------------------------------------------------------------------------- */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart_debug);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart_esp);
}

/* ------------------------------------------------------------------------- */
/* HAL 接收完成回调：一字节收到后写入环形缓冲并重新挂起下一次接收            */
/* ------------------------------------------------------------------------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart_debug)
    {
        /* 把 USART1 收到的字节写入调试环形缓冲 */
        ringbuf_write_one(&g_rb_debug_rx, (uint8_t)s_rx1_byte);
        /* 重新挂起接收，等待下一字节 */
        HAL_UART_Receive_IT(huart, (uint8_t *)&s_rx1_byte, 1);
    }
    else if (huart == &huart_esp)
    {
        /* 把 USART2（ESP8266）收到的字节写入其环形缓冲 */
        ringbuf_write_one(&g_rb_esp_rx, (uint8_t)s_rx2_byte);
        HAL_UART_Receive_IT(huart, (uint8_t *)&s_rx2_byte, 1);
    }
}

/* ------------------------------------------------------------------------- */
/* HAL 发送完成回调（保留，暂未在此使用，用于未来 DMA 发送场景）             */
/* ------------------------------------------------------------------------- */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
}

/* ------------------------------------------------------------------------- */
/* HAL 错误回调：发生接收溢出等错误时复位并重新挂起接收，保证系统健壮        */
/* ------------------------------------------------------------------------- */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart_debug)
    {
        /* 读取 SR 和 DR 以清除 ORE/FE/NE 等错误标志 */
        __HAL_UART_CLEAR_OREFLAG(huart);
        HAL_UART_Receive_IT(huart, (uint8_t *)&s_rx1_byte, 1);
    }
    else if (huart == &huart_esp)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        HAL_UART_Receive_IT(huart, (uint8_t *)&s_rx2_byte, 1);
    }
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
HAL_StatusTypeDef usart_irq_rx_start(void)
{
    HAL_StatusTypeDef st = HAL_UART_Receive_IT(&huart_debug, (uint8_t *)&s_rx1_byte, 1);
    if (st != HAL_OK)
    {
        return st;
    }
    return HAL_UART_Receive_IT(&huart_esp, (uint8_t *)&s_rx2_byte, 1);
}

HAL_StatusTypeDef usart_esp_rx_rearm(void)
{
    return HAL_UART_Receive_IT(&huart_esp, (uint8_t *)&s_rx2_byte, 1);
}

void *usart_irq_debug_get_priv(void)
{
    return (void *)&g_rb_debug_rx;
}