/**
 ******************************************************************************
 * @file    uart_config.h
 * @brief   UART 串口底层配置模块：
 *          - USART1：调试串口，连接电脑（上位机），波特率 115200 8N1
 *          - USART2：WiFi 串口，连接 ESP8266 模块，波特率 9600 8N1
 *
 * 引脚分配（STM32F103C8T6 默认映射，无需重映射）：
 *     USART1_TX = PA9     USART1_RX = PA10
 *     USART2_TX = PA2     USART2_RX = PA3
 *
 * 本模块在“User”目录内自包含地完成了：时钟(APB1/APB2)、GPIO、NVIC 中断
 * 以及 STM32 HAL UART 驱动初始化，无需改动 CubeMX 生成的源码即可直接使用。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __UART_CONFIG_H
#define __UART_CONFIG_H

#include "stm32f1xx_hal.h"
#include "common/ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* 串口参数宏定义（可在此处按实际硬件调整）                                    */
/* ------------------------------------------------------------------------- */
#define DEBUG_UART_BAUDRATE   115200U   /* 调试串口波特率 */
#define ESP_UART_BAUDRATE     9600U    /* ESP8266 串口波特率（需与模块固件一致，此处为 9600） */

/* 接收环形缓冲区大小（字节） */
#define DEBUG_RX_BUF_SIZE     256U      /* 调试串口接收缓冲 */
#define ESP_RX_BUF_SIZE       512U      /* ESP8266 接收缓冲（需容纳 MQTT 报文） */

/* ------------------------------------------------------------------------- */
/* 串口句柄（供上层模块使用）                                                 */
/* ------------------------------------------------------------------------- */
extern UART_HandleTypeDef huart_debug;   /**< USART1 调试串口 */
extern UART_HandleTypeDef huart_esp;     /**< USART2 ESP8266 串口 */

/* 接收环形缓冲区（ISR 写入，上层读取） */
extern ringbuf_t g_rb_debug_rx;          /**< 调试串口接收环形缓冲 */
extern ringbuf_t g_rb_esp_rx;            /**< ESP8266 接收环形缓冲 */

/* ------------------------------------------------------------------------- */
/* 接口函数                                                                    */
/* ------------------------------------------------------------------------- */

/** 初始化全部串口：USART1 调试 + USART2 ESP8266。
 *  调用后还需调用 usart_irq_rx_start() 开启接收中断。返回 HAL 状态。
 */
HAL_StatusTypeDef uart_init_all(void);

/** 向指定串口阻塞发送若干字节（带简单超时保护），返回实际发送长度 */
uint16_t uart_send(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len);

/** 运行时重设 USART2(ESP8266) 波特率。调用后需重新挂起接收中断。 */
void uart_esp_set_baud(uint32_t baud);

#ifdef __cplusplus
}
#endif

#endif /* __UART_CONFIG_H */