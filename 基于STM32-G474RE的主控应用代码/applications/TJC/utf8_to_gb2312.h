/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-06-15     17625       the first version
 */
#ifndef APPLICATIONS_TJC_UTF8_TO_GB2312_H_
#define APPLICATIONS_TJC_UTF8_TO_GB2312_H_

#include <stdint.h>

/**
 * @brief UTF-8 字符串转换为 GB2312 编码字符串
 * @param src       源 UTF-8 字符串（以 '\0' 结尾，或由 src_len 指定长度）
 * @param src_len   源字符串长度（字节数），若为 0 则自动计算 strlen
 * @param dst       目标缓冲区（需足够大）
 * @param dst_len   输出：转换后的字节数（不含结尾 '\0'）
 * @return 0:成功; -1:参数错误; -2:缓冲区不足（未实现溢出检查，请保证 dst 足够大）
 *
 * @note 转换后的 GB2312 字符串不会自动添加 '\0'，需调用者手动添加。
 * @note 本映射表只包含天气相关常用汉字（约 50 个），未覆盖的字符会被替换为 '?'。
 */
int utf8_to_gb2312(const uint8_t *src, uint32_t src_len,
                   uint8_t *dst, uint32_t *dst_len);


#endif /* APPLICATIONS_TJC_UTF8_TO_GB2312_H_ */
