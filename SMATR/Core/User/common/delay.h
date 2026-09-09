/**
 ******************************************************************************
 * @file    delay.h
 * @brief   定时与阻塞延时助手（基于 HAL 的 ms 节拍 HAL_GetTick）。
 *
 * 说明：
 *   - 提供毫秒级“忙等待”延时 delay_ms()；
 *   - 提供 timeout 计数结构，便于在“非阻塞主循环 + 带超时等待”的
 *     协作式调度中实现可靠的等待逻辑。
 ******************************************************************************
 */
#ifndef __DELAY_H
#define __DELAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 超时计时器对象 */
typedef struct
{
    uint32_t start_ms;   /**< 起始时间戳 */
    uint32_t duration;   /**< 目标时长（ms） */
} timeout_t;

/** 读取当前系统毫秒时间戳 */
uint32_t now_ms(void);

/** 忙等待延时 n 毫秒（基于 HAL 节拍，非阻塞于中断之外） */
void delay_ms(uint32_t n);

/** 启动一个超时计时器 */
void timeout_start(timeout_t *t, uint32_t duration_ms);

/** 检查超时计时器是否已到时间（true = 已超时） */
bool timeout_expired(const timeout_t *t);

#ifdef __cplusplus
}
#endif

#endif /* __DELAY_H */