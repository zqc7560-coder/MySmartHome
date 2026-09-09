/**
 ******************************************************************************
 * @file    ringbuf.h
 * @brief   环形缓冲区（Ring Buffer）—— 供 UART 中断服务程序与主程序之间传递数据。
 *
 * 说明：
 *   - 生产者（UART 接收中断 ISR）调用 ringbuf_write_one() 写入单字节；
 *   - 消费者（主循环 / 任务）调用 ringbuf_read_byte() / ringbuf_peek_byte() 读取；
 *   - 设计为“单生产者 / 单消费者”模式：ISR 只写，主程序只读，
 *     因此在读写指针不同时被修改的前提下无需加锁，天然具备中断安全性。
 *
 * 版本：1.0
 ******************************************************************************
 */
#ifndef __RINGBUF_H
#define __RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 环形缓冲区数据结构 */
typedef struct
{
    uint8_t  *buffer;     /**< 存储缓冲区基址（由调用者提供） */
    uint16_t  size;       /**< 缓冲区容量（字节） */
    uint16_t  head;       /**< 写入位置（生产者维护） */
    uint16_t  tail;       /**< 读取位置（消费者维护） */
    uint16_t  count;      /**< 当前已存字节数 */
} ringbuf_t;

/** 初始化环形缓冲区
 * @param rb   缓冲区对象
 * @param buf  外部提供的存储数组
 * @param size 存储数组大小（字节）
 */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t size);

/** 清空缓冲区（丢弃所有数据） */
void ringbuf_flush(ringbuf_t *rb);

/** 写入单字节（生产者/ISR 使用），缓冲满时返回 false */
bool ringbuf_write_one(ringbuf_t *rb, uint8_t byte);

/** 批量写入（生产者使用），返回实际写入字节数 */
uint16_t ringbuf_write_buf(ringbuf_t *rb, const uint8_t *data, uint16_t len);

/** 读取单字节（消费者使用），无数据返回 -1 */
int16_t ringbuf_read_byte(ringbuf_t *rb);

/** 查看单字节但不消费（返回 -1 表示无数据） */
int16_t ringbuf_peek_byte(const ringbuf_t *rb, uint16_t index);

/** 查看一段数据但不消费（拷贝到 dst），返回实际拷贝字节数 */
uint16_t ringbuf_peek_buf(const ringbuf_t *rb, uint8_t *dst, uint16_t len);

/** 丢弃前 n 个字节 */
uint16_t ringbuf_discard(ringbuf_t *rb, uint16_t n);

/** 当前可用（可读）字节数 */
uint16_t ringbuf_avail(const ringbuf_t *rb);

/** 当前空闲（可写）字节数 */
uint16_t ringbuf_free(const ringbuf_t *rb);

/** 是否为空 */
bool ringbuf_is_empty(const ringbuf_t *rb);

/** 在可用字节中搜索 pattern（不消费）。找到返回所在偏移，未找到返回 -1。
 *  p2p 交叉(跨尾部回绕)时通过临时拼接参与比较。
 */
int16_t ringbuf_search(const ringbuf_t *rb, const uint8_t *pattern, uint16_t plen);

/** 自检函数（用于 "test" 调试指令），返回 true 表示通过 */
bool ringbuf_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* __RINGBUF_H */