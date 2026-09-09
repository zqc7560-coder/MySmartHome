/**
 ******************************************************************************
 * @file    led.h
 * @brief   LED 灯接口模块：
 *          - 完成 GPIO 初始化（单只 LED）；
 *          - 提供开关 / 翻转 / 状态查询接口；
 *          - 引脚与有效电平可配置（改下方宏即可）。
 *
 * 默认使用蓝色药丸板(Blue Pill)板载 PC13 LED（低电平点亮，有源低）。
 * 若接外部 LED，只需修改 LED_GPIO_PORT / LED_GPIO_PIN /
 * LED_GPIO_CLK_ENABLE / LED_ACTIVE_LOW 四个宏。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __LED_H
#define __LED_H

#include <stdbool.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* LED 引脚与有效电平配置（可改）                                              */
/* ------------------------------------------------------------------------- */
#define LED_GPIO_PORT          GPIOC          /* 引脚所在 GPIO 端口 */
#define LED_GPIO_PIN           GPIO_PIN_13    /* 引脚号            */
#define LED_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOC_CLK_ENABLE()  /* 使能端口时钟 */
#define LED_ACTIVE_LOW         1              /* 1=低电平点亮(板载PC13)；0=高电平点亮 */

/** 初始化 LED GPIO（应用启动时调用一次）。 */
void led_init(void);

/** 设置 LED 亮灭：true=点亮，false=熄灭。 */
void led_set(bool on);

/** 点亮 LED。 */
void led_on(void);

/** 熄灭 LED。 */
void led_off(void);

/** 翻转 LED 状态。 */
void led_toggle(void);

/** 读取当前状态：true=点亮，false=熄灭。 */
bool led_get(void);

/**
 * 将 LED 状态上报到 OneNet（数据流 id 固定为 "led"，值 1=点亮 0=熄灭）。
 * 需在已连接状态下调用，返回 onenet 发布结果。
 */
int led_publish_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_H */