/**
 ******************************************************************************
 * @file    ringbuf.c
 * @brief   环形缓冲区的实现。
 ******************************************************************************
 */
#include "common/ringbuf.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* 初始化与清空                                                              */
/* ------------------------------------------------------------------------- */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t size)
{
    rb->buffer = buf;
    rb->size   = size;
    rb->head   = 0;
    rb->tail   = 0;
    rb->count  = 0;
}

void ringbuf_flush(ringbuf_t *rb)
{
    rb->head  = 0;
    rb->tail  = 0;
    rb->count = 0;
}

/* ------------------------------------------------------------------------- */
/* 基础判断                                                                  */
/* ------------------------------------------------------------------------- */
bool ringbuf_is_empty(const ringbuf_t *rb)
{
    return (rb->count == 0);
}

uint16_t ringbuf_avail(const ringbuf_t *rb)
{
    return rb->count;
}

uint16_t ringbuf_free(const ringbuf_t *rb)
{
    return (uint16_t)(rb->size - rb->count);
}

/* ------------------------------------------------------------------------- */
/* 写入                                                                      */
/* ------------------------------------------------------------------------- */
bool ringbuf_write_one(ringbuf_t *rb, uint8_t byte)
{
    if (rb->count >= rb->size)
    {
        /* 缓冲已满，丢弃该字节（UART 溢出保护） */
        return false;
    }
    rb->buffer[rb->head] = byte;
    rb->head = (uint16_t)((rb->head + 1U) % rb->size);
    rb->count++;
    return true;
}

uint16_t ringbuf_write_buf(ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    uint16_t written = 0;
    while (written < len)
    {
        if (!ringbuf_write_one(rb, data[written]))
        {
            break;   /* 空间不足 */
        }
        written++;
    }
    return written;
}

/* ------------------------------------------------------------------------- */
/* 读取                                                                      */
/* ------------------------------------------------------------------------- */
int16_t ringbuf_read_byte(ringbuf_t *rb)
{
    if (rb->count == 0)
    {
        return -1;
    }
    uint8_t byte = rb->buffer[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1U) % rb->size);
    rb->count--;
    return (int16_t)byte;
}

/* ------------------------------------------------------------------------- */
/* 查看（不消费）                                                            */
/* ------------------------------------------------------------------------- */
int16_t ringbuf_peek_byte(const ringbuf_t *rb, uint16_t index)
{
    if (index >= rb->count)
    {
        return -1;
    }
    uint16_t pos = (uint16_t)((rb->tail + index) % rb->size);
    return (int16_t)rb->buffer[pos];
}

uint16_t ringbuf_peek_buf(const ringbuf_t *rb, uint8_t *dst, uint16_t len)
{
    uint16_t n = len;
    if (n > rb->count)
    {
        n = rb->count;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        uint16_t pos = (uint16_t)((rb->tail + i) % rb->size);
        dst[i] = rb->buffer[pos];
    }
    return n;
}

uint16_t ringbuf_discard(ringbuf_t *rb, uint16_t n)
{
    uint16_t actual = n;
    if (actual > rb->count)
    {
        actual = rb->count;
    }
    rb->tail  = (uint16_t)((rb->tail + actual) % rb->size);
    rb->count = (uint16_t)(rb->count - actual);
    return actual;
}

/* ------------------------------------------------------------------------- */
/* 子串搜索（不消费）                                                        */
/* ------------------------------------------------------------------------- */
int16_t ringbuf_search(const ringbuf_t *rb, const uint8_t *pattern, uint16_t plen)
{
    if ((pattern == NULL) || (plen == 0) || (rb->count == 0) || (plen > rb->count))
    {
        return -1;
    }

    /* 从偏移 0 到 (count-plen) 逐位置比较，支持跨尾部回绕 */
    uint16_t max_start = (uint16_t)(rb->count - plen);
    for (uint16_t start = 0; start <= max_start; start++)
    {
        bool match = true;
        for (uint16_t i = 0; i < plen; i++)
        {
            uint16_t pos = (uint16_t)((rb->tail + start + i) % rb->size);
            if (rb->buffer[pos] != pattern[i])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return (int16_t)start;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/* 自检                                                                    */
/* ------------------------------------------------------------------------- */
bool ringbuf_selftest(void)
{
    static uint8_t storage[10];
    ringbuf_t rb;
    ringbuf_init(&rb, storage, sizeof(storage));

    /* 1) 空读应返回 -1 */
    if (ringbuf_read_byte(&rb) != -1) return false;

    /* 2) 写入并回绕测试 */
    for (uint8_t i = 0; i < 10; i++)
    {
        if (!ringbuf_write_one(&rb, i)) return false;
    }
    /* 现在已满 */
    if (ringbuf_write_one(&rb, 0xFF)) return false;   /* 满时应拒绝 */

    /* 消费 3 个，再写 3 个，触发回绕 */
    (void)ringbuf_discard(&rb, 3);
    uint8_t tail_data[3] = {0x1A, 0x2B, 0x3C};
    ringbuf_write_buf(&rb, tail_data, 3);

    /* 预期序列：3,4,5,6,7,8,9,0x1A,0x2B,0x3C */
    uint8_t expect[10] = {3,4,5,6,7,8,9,0x1A,0x2B,0x3C};
    for (uint8_t i = 0; i < 10; i++)
    {
        if ((uint8_t)ringbuf_read_byte(&rb) != expect[i]) return false;
    }

    /* 3) search 测试 */
    uint8_t pat[]   = {7,8,9};
    uint8_t patMiss[] = {0xA,0xB};
    ringbuf_init(&rb, storage, sizeof(storage));
    for (uint8_t i = 0; i < 10; i++) { ringbuf_write_one(&rb, i); }
    if (ringbuf_search(&rb, pat, 3) != 7) return false;
    if (ringbuf_search(&rb, patMiss, 2) != -1) return false;

    return true;
}