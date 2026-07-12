/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-02     17625       the first version
 */
#ifndef APPLICATIONS_SENSORS_H_
#define APPLICATIONS_SENSORS_H_

#include "common.h"

/* 1. 选择存储地址：
 *    - 该地址位于 BANK2 的最后 4KB 内（0x0807F000 ~ 0x0807FFFF），远离代码区。
 *    - 2KB 对齐，满足双 BANK 的页大小要求。
 *    - 该页在默认链接脚本中不会被分配（因为链接脚本通常只使用第一个 BANK）
 */
#define SGP30_BASELINE_FLASH_ADDR  0x0807F000

/* ==================== 传感器线程配置 ==================== */
// 土壤传感器线程
#define THREAD_SOIL_PRIORITY     6
#define THREAD_SOIL_STACK_SIZE   768
#define THREAD_SOIL_TICK         1000  // 1s采集间隔

// SHT3X温度湿度传感器线程
#define THREAD_SHT3X_PRIORITY    6
#define THREAD_SHT3X_STACK_SIZE  768
#define THREAD_SHT3X_TICK        800  // 800ms采集间隔

// BH1750光强传感器线程
#define THREAD_BH1750_PRIORITY   6
#define THREAD_BH1750_STACK_SIZE 768
#define THREAD_BH1750_TICK       800  // 800ms采集间隔

// SGP30空气质量传感器线程
#define THREAD_SGP30_PRIORITY    6
#define THREAD_SGP30_STACK_SIZE  768
#define THREAD_SGP30_TICK        1000 // 1s采集间隔

#define HUMIDITY_COMP_INTERVAL 10   // 每5次调用才实际执行一次补偿

/* AI识别线程（通过串口接收ESP32-CAM数据） */
#define THREAD_AI_PRIORITY     6
#define THREAD_AI_STACK_SIZE   1792
#define THREAD_AI_TICK         0        // 不用于周期性，实际使用阻塞读取
/* 规定识别次数（可调整） */
#define AI_VOTE_COUNT    5

// DS18B20 温度传感器线程
#define THREAD_DS18B20_PRIORITY   6
#define THREAD_DS18B20_STACK_SIZE 1024
#define THREAD_DS18B20_TICK       1000  // 1秒采集一次


/* ==================== AI 串口配置 ==================== */
#define AI_UART_DEVICE     "uart3"      // 连接 ESP32-CAM 的串口设备
#define AI_UART_BAUD       115200
#define AI_UART_DATA_BITS  8
#define AI_UART_STOP_BITS  1
#define AI_UART_PARITY     0            // 无奇偶校验
#define RX_BUFFER_SIZE     2048

/* 土壤传感器配置结构体 */
typedef struct {
    const char* adc_name;   // ADC设备名称
    int channel;           // ADC通道
    int air_value;         // 干燥状态ADC值（空气中）
    int water_value;       // 湿润状态ADC值（水中）
    int interval;          // 湿度区间间隔
    const char* name;      // 传感器名称
} soil_sensor_t;


/* 传感器初始化 */
rt_err_t env_sensors_init(void);
rt_err_t ai_uart_init(void);

/* 线程管理 */
int sensors_threads_init(void);
void sensors_threads_stop(void);

/* 传感器读取 */
void read_soil_sensor(void);
void read_sht3x_sensor(void);
void read_bh1750_sensor(void);
void read_sgp30_sensor(void);
void read_ds18b20_sensor(void);

#endif /* APPLICATIONS_SENSORS_H_ */
