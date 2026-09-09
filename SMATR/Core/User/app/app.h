/**
 ******************************************************************************
 * @file    app.h
 * @brief   应用编排层：
 *          - 协调两条 UART（调试 + ESP8266）与 OneNet MQTT 全链路；
 *          - 实现命令行调试接口（help/ping/status/wifi/publish/...）；
 *          - 维护“连接 → 上报 → 重连”状态机；
 *          - 提供自检测试用例。
 ******************************************************************************
 */
#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

/** 应用初始化：串口、调试控制台、OneNet、命令注册。主程序开机调用一次。 */
void app_init(void);

/** 应用主循环体：需在主循环中高频调用（完成收发处理）。 */
void app_loop(void);

/** 运行内置自测（环形缓冲等），返回 true 表示通过 */
int app_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */