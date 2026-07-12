/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-31     17625       the first version
 */
#ifndef APPLICATIONS_TJC_TJC_USART_HMI_H_
#define APPLICATIONS_TJC_TJC_USART_HMI_H_

#include <stdio.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/**
    打印到屏幕串口
*/

// 使用 RT-Thread 的设备框架
#define TJC_UART_NAME "uart2"  // 根据实际配置修改串口设备名称
extern struct rt_device *tjc_uart_dev;


void tjc_show_chinese(const char *widget, const char *utf8_str);
rt_err_t tjc_uart_init(void);
void tjc_send_string(char* str);
void tjc_send_txt(char* objname, char* attribute, char* txt);
void tjc_send_val(char* objname, char* attribute, int val);
void tjc_send_nstring(char* str, unsigned char str_length);
void initRingBuffer(void);
void write1ByteToRingBuffer(uint8_t data);
void deleteRingBuffer(uint16_t size);
uint16_t getRingBufferLength(void);
uint8_t read1ByteFromRingBuffer(uint16_t position);




#define RINGBUFFER_LEN  (500)     //定义最大接收字节数 500

#define usize getRingBufferLength()
#define code_c() initRingBuffer()
#define udelete(x) deleteRingBuffer(x)
#define u(x) read1ByteFromRingBuffer(x)

extern uint8_t RxBuffer[1];
extern struct rt_ringbuffer *rx_ringbuffer;


#endif /* APPLICATIONS_TJC_TJC_USART_HMI_H_ */
