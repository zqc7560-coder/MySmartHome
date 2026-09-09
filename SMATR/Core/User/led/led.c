/**
 ******************************************************************************
 * @file    led.c
 * @brief   LED 灯接口模块实现。
 ******************************************************************************
 */
#include "led/led.h"
#include "onenet/onenet.h"

/* 当前状态：true=点亮 */
static bool s_led_on = false;

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */
void led_init(void)
{
    /* 使能端口时钟并配置为推挽输出 */
    LED_GPIO_CLK_ENABLE();

    GPIO_InitTypeDef gpio;
    gpio.Pin   = LED_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;     /* 推挽输出 */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;     /* 低频即可驱动 LED */
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio);

    led_off();                            /* 上电默认熄灭 */
}

void led_set(bool on)
{
    s_led_on = on;
    /* 根据有效电平计算应写入的电平 */
    GPIO_PinState level = (on ^ (bool)LED_ACTIVE_LOW) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, level);
}

void led_on(void)
{
    led_set(true);
}

void led_off(void)
{
    led_set(false);
}

void led_toggle(void)
{
    led_set(!s_led_on);
}

bool led_get(void)
{
    return s_led_on;
}

int led_publish_state(void)
{
    /* 以物模型属性上报 LED 状态（标识符由 ONENET_LED_PROP_ID 指定，1=亮/0=灭） */
    return onenet_report_property_b(ONENET_LED_PROP_ID, s_led_on ? 1 : 0);
}