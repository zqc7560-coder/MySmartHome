/**
 ******************************************************************************
 * @file    delay.c
 * @brief   定时与阻塞延时助手的实现。
 ******************************************************************************
 */
#include "common/delay.h"
#include "stm32f1xx_hal.h"   /* HAL_GetTick */

uint32_t now_ms(void)
{
    return HAL_GetTick();
}

void delay_ms(uint32_t n)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < n)
    {
        /* 空转等待，直到达到 n 毫秒 */
    }
}

void timeout_start(timeout_t *t, uint32_t duration_ms)
{
    if (t == NULL)
    {
        return;
    }
    t->start_ms = HAL_GetTick();
    t->duration = duration_ms;
}

bool timeout_expired(const timeout_t *t)
{
    if (t == NULL)
    {
        return true;
    }
    /* 使用无符号减法，自然处理 HAL 节拍回绕 */
    uint32_t elapsed = (uint32_t)(HAL_GetTick() - t->start_ms);
    return (elapsed >= t->duration);
}