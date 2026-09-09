# MySmartHome —— 双 UART 串口通信 + ESP8266 MQTT 接入 OneNet

本文档说明如何在现有 STM32 项目（`SMATR`，STM32F103C8T6，HAL + STM32CubeIDE）中集成和使用本套件。

## 1. 功能总览

本套件实现了两个相互独立的 UART 串口：

| 串口 | 外设 | 引脚 | 波特率 | 用途 | 数据方向 |
|------|------|------|--------|------|----------|
| 调试串口 | USART1 | PA9(TX) / PA10(RX) | 115200 8N1 | 连接电脑，上位机调试 | STM32 ⇄ 电脑 |
| WiFi 串口 | USART2 | PA2(TX) / PA3(RX) | 115200 8N1 | 连接 ESP8266 模块 | STM32 ⇄ ESP8266 → OneNet |

- 调试串口：格式化日志输出 + 接收电脑端命令行指令（`help`/`status`/`wifi`/`publish`...）。
- WiFi 串口：ESP8266 AT 指令 → WiFi 联网 → TCP 透传 → 最小化 **MQTT 3.1.1** 协议栈 → 接入 **OneNet 云平台**，实现数据上报、命令下发订阅与心跳保活。

## 2. 目录结构（全部位于 `Core/User`）

```
Core/User/
├── app/
│   ├── app.c/h        应用编排：状态机、命令行分发、周期上报、重连看门狗、自测
├── common/
│   ├── ringbuf.c/h    环形缓冲（ISR 写 / 主循环读，单生产单消费无需加锁）
│   └── delay.c/h      延时 / 超时计时工具
├── debug/
│   └── debug.c/h      调试串口：格式化输出 + 指令解析
├── esp8266/
│   └── esp8266.c/h    ESP8266 AT 驱动（联网、TCP、透传模式）
├── mqtt/
│   └── mqtt_client.c/h 最小化 MQTT 3.1.1 客户端协议栈
├── onenet/
│   └── onenet.c/h     OneNet 接入：连接、数据点上报、命令订阅、保活
├── port/
│   └── usart_irq.c/h  USART1 / USART2 中断服务程序
└── README.md          本文档
```

## 3. 已完成的工程配置（本套件自动完成）

1. `Core/Inc/stm32f1xx_hal_conf.h`：已使能 `HAL_UART_MODULE_ENABLED`。
2. `Drivers/STM32F1xx_HAL_Driver/`：已从本机 STM32Cube FW_F1 V1.8.7 补充了
   `stm32f1xx_hal_uart.c / .h`（原工程未使能 UART 而未包含）。
3. `.cproject`：C 编译器 include 路径已加入 `../Core/User`（Debug 与 Release 均加入）。
4. `Core/Src/main.c`：已接入 `app_init()` 与 while 循环中的 `app_loop()`。

## 4. 配置（必看）

在 `Core/User/onenet/onenet.h` 中按实际情况填写：

```c
#define ONENET_PRODUCT_ID  "你的产品ID"
#define ONENET_DEVICE_ID   "你的设备ID(约16位数字)"
#define ONENET_API_KEY     "你的产品/设备访问密钥"
#define ONENET_WIFI_SSID   "你的WiFi名"
#define ONENET_WIFI_PASS   "你的WiFi密码"
```

设备在 OneNet 控制台创建时，请选择 **MQTT 协议**了接入方式。默认接入点：
主机 `mqtts.heclouds.com`、端口 `1883`（明文 TCP，ESP8266 透传，MCU 端做 MQTT 层）。

## 5. 编译与集成

1. 用 STM32CubeIDE 打开 `SMATR` 工程。
2. 直接 **Build** 即可（IDE 会自动重扫 `Core`、`Drivers` 源码容器，编译新增的 `.c`）。
3. 下载到 STM32F103C8T6，接线：
   - USART1 (PA9/PA10) —— USB-TTL → 电脑（115200）。
   - USART2 (PA2/PA3) —— ESP8266（TXD<RXD、RXD<TXD）。
   - 注意：PA9/PA10、PA2/PA3 与 Download/SWD(JTAG) 引脚不冲突，本工程 SWD 使用 PA13/PA14。

> 提示：如果此前 IDE 中 `.cproject` 的改动未被识别，可在工程属性 →
> C/C++ Build / C/C++ General → Paths and Symbols 中确认 `../Core/User` 已被列入 include 路径。

## 6. 上位机调试指令（通过调试串口发送，回车结束）

| 指令 | 作用 |
|------|------|
| `help` | 显示全部指令 |
| `status` | 显示系统与连接状态 |
| `connect` | 手动连接 OneNet |
| `disconnect` | 手动断开 |
| `wifi <ssid> <pass>` | 修改 WiFi 并重连 |
| `publish [value]` | 上报一个数值数据点（默认模拟温度） |
| `ping` | 查看心跳保活状态 |
| `test` | 运行内置自测（环形缓冲单元测试等） |
| `reset` | 软件复位 ESP8266 并重连 |

## 7. 测试用例

### 7.1 调试串口（USART1）——上位机验证
1. 上电后串口应打印启动横幅与应用状态。
2. 发送 `test`，应输出 `环形缓冲自测 通过`、`调试串口(UART1)工作正常`。
3. 发送 `status`，观察连接状态实时反馈。

### 7.2 ESP8266 + OneNet (USART2) 链路验证
1. 上位机发送 `connect`（或等待自动连接）。
2. 观察日志：`正在连接 OneNet ...` → `连接成功！` → `已订阅命令主题`。
3. 默认每 5s 自动上报一次 `temperature` 数据点（可在 OneNet 控制台「设备/数据流」看到）。
4. 在 OneNet 控制台向该设备下发指令，MCU 会打印 `[ONENET] 收到主题 '$creq/...' 命令: ...`。
5. 拔掉 WiFi / 断开电路模拟异常，观察重新连接（重连看门狗机制）。

### 7.3 代码级单元自测
- `ringbuf_selftest()`：环形缓冲满/空/回绕/搜索的边界用例。
- 模块说明见各 `.c` 文件头注释。

## 8. 错误处理与异常恢复

- UART 溢出（ORE/FE/NE）：`HAL_UART_ErrorCallback` 读取 SR/DR 清标志并重挂接收。
- ESP8266 指令超时：AT 命令带超时，失败即断言为 `esp_*` 错误并进入重试。
- MQTT 连接被拒：解析 CONNACK 返回码并上报日志。
- 链路异常：应用使用「周期上报失败」+「连接看门狗」双机制自动重连。

## 9. 资源占用提示

STM32F103C8T6 只有 **20KB RAM**：
- 环形缓冲：调试 256B + ESP 512B。
- MQTT：发送缓冲 320B + 接收缓冲 256B。
- 其余静态缓冲均较小（约 <2KB）。
- 本工程原有 FreeRTOS 内核（heap 8KB）即便未启动调度也占用静态 RAM；
  若编译链接接近 RAM 上限，可在未使用 RTOS 的前提下移除 FreeRTOS 源码以释放空间。