/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-31     17625       the first version
 */
#ifndef APPLICATIONS_EXTERNAL_COMM_H_
#define APPLICATIONS_EXTERNAL_COMM_H_

#include "common.h"
#include "./TJC/tjc_usart_hmi.h"
#include <stdio.h>
#include <cJSON.h>                     /* 添加cJSON头文件 */
#include "onenet.h"                      /* 添加onenet头文件，获取ONENET_TOPIC_DP等宏 */
#include <webclient.h>   /* webclient 相关函数 */
#include <stdlib.h>      /* atoi, atof */

#define ONENET_TOPIC_DP "$sys/" ONENET_INFO_PROID "/" ONENET_INFO_DEVID "/thing/property/post"

/* ==================== 显示线程配置 ==================== */
#define THREAD_COMM_PRIORITY     7
#define THREAD_COMM_STACK_SIZE   1024
#define THREAD_COMM_TICK         100  // 100ms更新间隔

/* ==================== 上传线程配置 ==================== */
#define THREAD_UPLOAD_PRIORITY     8
#define THREAD_UPLOAD_STACK_SIZE   2304
#define THREAD_UPLOAD_TICK         6000

/* ==================== 天气线程配置 ==================== */
#define THREAD_WEATHER_PRIORITY     8
#define THREAD_WEATHER_STACK_SIZE    384     // HTTP请求可能需要较大栈
#define WEATHER_INTERVAL_MS         3600000     // 1小时更新一次，可根据需要调整

/* ==================== NTP线程配置 ==================== */
#define THREAD_NTP_PRIORITY     8          // 与天气线程同级，也可适当调整
#define THREAD_NTP_STACK_SIZE   384          // NTP同步栈需求较小
#define NTP_SYNC_INTERVAL_MS    (1800 * 1000)    // 每半小时同步一次

/* ==================== 网络工作线程配置 ==================== */
#define NET_WORKER_PRIORITY     8          // 网络工作线程优先级
#define NET_WORKER_STACK_SIZE   3072       // 栈大小
#define NET_REQ_QUEUE_SIZE      8         // 消息队列容量

/* 天气API配置（以心知天气为例） */
#define WEATHER_API_KEY             "   "
#define WEATHER_LOCATION             "suzhou"  // 地级市拼音
#define WEATHER_URL                 "http://api.seniverse.com/v3/weather/daily.json?key="WEATHER_API_KEY"&location="WEATHER_LOCATION"&language=zh-Hans&unit=c&start=0&days=3"

/* 上传间隔（毫秒） */
#define MAX_BATCH_SIZE  4   // 单次最多处理10条
/* 断线重连检查间隔（毫秒） */
#define RECONNECT_CHECK_MS     5000

/* 命令帧协议定义 */
#define FRAME_HEADER_1           0x88
#define FRAME_HEADER_2           0x77
#define FRAME_HEADER_3           0x88
#define FRAME_TAIL_1             0xFF
#define FRAME_TAIL_2             0xFF
#define FRAME_TAIL_3             0xFF
#define FRAME_LENGTH             10    // 命令帧总长度

/* 命令来源 */
#define CMD_SOURCE_LOCAL         0x00
#define CMD_SOURCE_CLOUD         0x01

/* 消息类型 */
#define CMD_TYPE_BROADCAST       0x00
#define CMD_TYPE_FAN             0x01
#define CMD_TYPE_PUMP            0x02
#define CMD_TYPE_LED             0x03
#define CMD_TYPE_BUZZER          0x04
#define CMD_TYPE_ATOMIZ          0x05
#define CMD_TYPE_AUDIO           0x06   /* 音频控制 */

/* 运行模式 */
#define MODE_MANUAL              0x00
#define MODE_AUTO                0x01

/* 开关状态 */
#define STATE_OFF                0x00
#define STATE_ON                 0x01

/* 状态图片 ID 宏定义（根据实际 TJC 工程图片编号调整） */
#define PIC_GREEN   13
#define PIC_YELLOW  14
#define PIC_RED     15

/* 网络请求类型 */
typedef enum {
    NET_REQ_WEATHER,            // 天气请求
    NET_REQ_NTP,                // NTP同步
    NET_REQ_MQTT_PUB,            // MQTT发布（携带JSON字符串）
    NET_REQ_CMD_RSP             // OneNET命令响应处理（云平台下发指令）
} net_req_type_t;

/* 网络请求消息结构 */
typedef struct {
    net_req_type_t type;        // 请求类型
    union {
        struct {
            /* 天气请求无需额外数据 */
        } weather;
        struct {
            /* NTP请求无需额外数据 */
        } ntp;
        struct {
            char *json_str;     // JSON字符串（动态分配，由工作线程释放）
            size_t json_len;    // 字符串长度
        } mqtt_pub;

        struct {
            uint8_t *data;      // 原始命令数据（动态分配）
            size_t len;         // 数据长度
        } cmd_rsp;

    } data;
} net_req_msg_t;

extern rt_mq_t net_req_queue;          // 网络请求队列
extern rt_bool_t net_worker_running;   // 工作线程运行标志
extern rt_thread_t net_worker_thread;  // 工作线程句柄


extern rt_bool_t comm_running;
extern rt_thread_t comm_thread;
extern rt_bool_t weather_running;
extern rt_thread_t weather_thread;
extern rt_bool_t upload_running;
extern rt_thread_t upload_thread;

/* 函数声明 */

int comm_thread_init(void);
void system_cleanup(void);
rt_bool_t parse_tjc_command(uint8_t *frame, command_t *cmd);
void process_received_commands(void);
void process_display_thread(void);

#endif /* APPLICATIONS_EXTERNAL_COMM_H_ */
