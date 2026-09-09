/**
 ******************************************************************************
 * @file    uart_config.c
 * @brief   UART 串口底层配置模块的实现。
 ******************************************************************************
 */
#include "uart/uart_config.h"
#include "main.h"   /* 提供 Error_Handler() */

/* ------------------------------------------------------------------------- */
/* 全局句柄与环形缓冲存储                                                     */
/* ------------------------------------------------------------------------- */
UART_HandleTypeDef huart_debug;                     /* USART1 调试串口 */
UART_HandleTypeDef huart_esp;                       /* USART2 ESP8266 串口 */

static uint8_t s_debug_rx_storage[DEBUG_RX_BUF_SIZE]; /* 调试串口接收缓冲存储数组 */
static uint8_t s_esp_rx_storage[ESP_RX_BUF_SIZE];     /* ESP8266 接收缓冲存储数组 */

ringbuf_t g_rb_debug_rx;
ringbuf_t g_rb_esp_rx;

/* ------------------------------------------------------------------------- */
/* 静态函数声明                                                              */
/* ------------------------------------------------------------------------- */
static void MX_USART1_UART_Init(UART_HandleTypeDef *huart, uint32_t baud);
static void MX_USART2_UART_Init(UART_HandleTypeDef *huart, uint32_t baud);

/* ------------------------------------------------------------------------- */
/* HAL MSP 回调：为每个 UART 使能时钟并配置 GPIO 与中断                       */
/* STM32 HAL UART 驱动在 HAL_UART_Init() 内部会调用本弱函数的强覆盖实现。        */
/* ------------------------------------------------------------------------- */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (huart->Instance == USART1)
    {
        /* --- USART1 (调试串口, APB2): PA9=TX, PA10=RX --- */
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin       = GPIO_PIN_9;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);          /* TX */

        GPIO_InitStruct.Pin       = GPIO_PIN_10;
        GPIO_InitStruct.Mode      = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);          /* RX */

        /* 配置并开启 USART1 全局中断 */
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    else if (huart->Instance == USART2)
    {
        /* --- USART2 (ESP8266 串口, APB1): PA2=TX, PA3=RX --- */
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin       = GPIO_PIN_2;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);          /* TX */

        GPIO_InitStruct.Pin       = GPIO_PIN_3;
        GPIO_InitStruct.Mode      = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);          /* RX */

        /* 配置并开启 USART2 全局中断 */
        HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
}

/* ------------------------------------------------------------------------- */
/* 单个串口的初始化（波特率 / 数据位 / 停止位 / 校验位 / 流控）                 */
/* ------------------------------------------------------------------------- */

/** USART1 调试串口配置：115200, 8 数据位, 1 停止位, 无校验, 无流控 */
static void MX_USART1_UART_Init(UART_HandleTypeDef *huart, uint32_t baud)
{
    huart->Instance          = USART1;
    huart->Init.BaudRate     = baud;
    huart->Init.WordLength   = UART_WORDLENGTH_8B;
    huart->Init.StopBits     = UART_STOPBITS_1;
    huart->Init.Parity       = UART_PARITY_NONE;
    huart->Init.Mode         = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    /* 失败则进入错误处理（开发期便于通过调试器定位） */
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        Error_Handler();
    }
}

/** USART2 ESP8266 串口配置：115200, 8 数据位, 1 停止位, 无校验, 无流控 */
static void MX_USART2_UART_Init(UART_HandleTypeDef *huart, uint32_t baud)
{
    huart->Instance          = USART2;
    huart->Init.BaudRate     = baud;
    huart->Init.WordLength   = UART_WORDLENGTH_8B;
    huart->Init.StopBits     = UART_STOPBITS_1;
    huart->Init.Parity       = UART_PARITY_NONE;
    huart->Init.Mode         = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* ------------------------------------------------------------------------- */

HAL_StatusTypeDef uart_init_all(void)
{
    /* 初始化两条串口的接收环形缓冲 */
    ringbuf_init(&g_rb_debug_rx, s_debug_rx_storage, DEBUG_RX_BUF_SIZE);
    ringbuf_init(&g_rb_esp_rx,   s_esp_rx_storage,   ESP_RX_BUF_SIZE);

    MX_USART1_UART_Init(&huart_debug, DEBUG_UART_BAUDRATE);
    MX_USART2_UART_Init(&huart_esp,   ESP_UART_BAUDRATE);

    /* 注意：接收中断的挂起放在 port/usart_irq 模块（usart_irq_rx_start()）
     * 调用，因为单字节接收中转用的字节槽位归属该模块管理。 */
    return HAL_OK;
}

uint16_t uart_send(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len)
{
    if ((huart == NULL) || (data == NULL) || (len == 0))
    {
        return 0;
    }
    /* 阻塞发送，设置 2s 超时避免硬件异常时永久卡死 */
    HAL_StatusTypeDef st = HAL_UART_Transmit(huart, (uint8_t *)data, len, 2000U);
    return (st == HAL_OK) ? len : 0;
}

void uart_esp_set_baud(uint32_t baud)
{
    /* 复用静态初始化函数以目标波特率重新配置 USART2（内部会调用 HAL_UART_Init）； */
    /* 由于重初始化会重置 HAL 句柄状态，调用方需随后重新挂起接收中断。 */
    MX_USART2_UART_Init(&huart_esp, baud);
}