/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-06-15     17625       the first version
 */
#include "utf8_to_gb2312.h"
#include <string.h>

/* ==================== 映射表（Unicode → GB2312） ==================== */
/* 注：GB2312 编码值均为高字节在前（大端） */

typedef struct {
    uint16_t unicode;
    uint16_t gb2312;
} unicode_gb2312_map_t;

static const unicode_gb2312_map_t g_weather_map[] = {
        {0x4E1C, 0xB6AB},   // 东
        {0x4E91, 0xD4C6},   // 云
        {0x505C, 0xCDA3},   // 停
        {0x52A8, 0xB6AF},   // 动 ← 新增
        {0x5317, 0xB1B1},   // 北
        {0x5357, 0xC4CF},   // 南
        {0x542F, 0xC6F4},   // 启
        {0x5DF2, 0xD2D1},   // 已
        {0x5F00, 0xBFAA},   // 开
        {0x672A, 0xCEB4},   // 未
        {0x6B62, 0xD6B9},   // 止
        {0x77E5, 0xD6AA},   // 知
        {0x7EA2, 0xBAEC},   // 红
        {0x7EA7, 0xBCB6},   // 级
        {0x7EFF, 0xC2CC},   // 绿
        {0x83C7, 0xB9BD},   // 菇
        {0x84DD, 0xC0B6},   // 蓝
        {0x897F, 0xCEF7},   // 西
        {0x98CE, 0xB7E7},   // 风
        {0x9EC4, 0xBBC6},   // 黄
};

#define MAP_SIZE (sizeof(g_weather_map) / sizeof(g_weather_map[0]))

/* ==================== 二分查找 ==================== */
static uint16_t unicode_to_gb2312(uint16_t unicode)
{
    int low = 0, high = MAP_SIZE - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (g_weather_map[mid].unicode == unicode)
            return g_weather_map[mid].gb2312;
        else if (g_weather_map[mid].unicode < unicode)
            low = mid + 1;
        else
            high = mid - 1;
    }
    return 0; // 未找到
}

/* ==================== UTF-8 解码 ==================== */
/* 返回 Unicode 码点，并移动 src 指针 */
static uint16_t decode_utf8(const uint8_t **src)
{
    const uint8_t *p = *src;
    uint16_t code = 0;

    if ((p[0] & 0x80) == 0) {          // 1 byte: 0xxxxxxx
        code = p[0];
        *src += 1;
    } else if ((p[0] & 0xE0) == 0xC0) { // 2 bytes: 110xxxxx 10xxxxxx
        if ((p[1] & 0xC0) != 0x80) goto invalid;
        code = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *src += 2;
    } else if ((p[0] & 0xF0) == 0xE0) { // 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) goto invalid;
        code = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *src += 3;
    } else {
        goto invalid;
    }
    return code;

invalid:
    // 无效 UTF-8，跳过一字节，返回替换字符
    *src += 1;
    return 0xFFFD; // �
}

/* ==================== 主转换函数 ==================== */
int utf8_to_gb2312(const uint8_t *src, uint32_t src_len,
                   uint8_t *dst, uint32_t *dst_len)
{
    if (!src || !dst || !dst_len)
        return -1;

    if (src_len == 0)
        src_len = strlen((const char*)src);

    const uint8_t *src_end = src + src_len;
    uint8_t *dst_start = dst;

    while (src < src_end) {
        uint16_t unicode = decode_utf8(&src);
        if (unicode < 0x80) {
            // ASCII 直接复制
            *dst++ = (uint8_t)unicode;
        } else if (unicode == 0xFFFD) {
            // 无效编码，替换为 '?'
            *dst++ = '?';
        } else {
            uint16_t gb = unicode_to_gb2312(unicode);
            if (gb == 0) {
                // 未找到映射，替换为 '?'
                *dst++ = '?';
            } else {
                // GB2312 高字节在前
                *dst++ = (uint8_t)(gb >> 8);
                *dst++ = (uint8_t)(gb & 0xFF);
            }
        }
    }

    *dst_len = (uint32_t)(dst - dst_start);
    return 0;
}
