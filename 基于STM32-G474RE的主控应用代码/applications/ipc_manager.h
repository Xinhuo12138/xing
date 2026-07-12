/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-05     17625       Complete communication header
 */
#ifndef APPLICATIONS_IPC_MANAGER_H_
#define APPLICATIONS_IPC_MANAGER_H_

#include "common.h"

/* ==================== 邮箱配置 ==================== */
#define MAILBOX_SIZE            8   // 邮箱大小

/* ==================== 传感器消息队列配置 ==================== */
#define MSG_QUEUE_ITEM_SIZE      sizeof(sensor_msg_t)

/* 队列配置 */
#define DISPLAY_QUEUE_SIZE      24   // 显示队列 - 需要实时性，队列较小
#define UPLOAD_QUEUE_SIZE       8    // 上传队列 - 上传间隔较长，队列可以较小
#define FUSION_QUEUE_SIZE       8   // 融合队列大小

/* ==================== 执行机专用消息队列配置 ==================== */
#define FAN_QUEUE_SIZE           4      // 风扇专用队列
#define PUMP_QUEUE_SIZE          4      // 水泵专用队列
#define LED_QUEUE_SIZE           4      // LED专用队列
#define BUZZER_QUEUE_SIZE        4      // 蜂鸣器专用队列
#define AUDIO_QUEUE_SIZE         4      // 音频专用队列
#define ATOMIZ_QUEUE_SIZE        4      // 加湿器专用队列

#define CMD_QUEUE_ITEM_SIZE     sizeof(command_t)
/* ==================== 内存池配置 ==================== */
#define CMD_POOL_MAX_CNT  24      // 最多同时缓存32条命令
#define CMD_POOL_ITEM_SIZE      sizeof(command_t)

/* 验证宏 */
#define SENSOR_MSG_SIZE_CHECK() \
    RT_ASSERT(MSG_QUEUE_ITEM_SIZE == sizeof(sensor_msg_t)); \
    LOG_I("sensor_msg_t size: %d, MSG_QUEUE_ITEM_SIZE: %d", sizeof(sensor_msg_t), MSG_QUEUE_ITEM_SIZE)


/* ==================== 事件集配置 ==================== */
#define EVENT_ALARM_GENERAL     0x1000000   /* 统一报警事件 */
#define EVENT_ALARM_ALL_CLEAR   0x100000 // 所有报警清除
#define EVENT_BUZZER_TIMEOUT    0x200000 // 蜂鸣器状态机定时器到期事件
// 天气数据更新事件（分发给不同用途的线程）
#define EVENT_WEATHER_UPLOAD   0x400000   // 通知上传线程
#define EVENT_WEATHER_DISPLAY  0x800000   // 通知显示线程


rt_err_t send_to_mq(rt_mq_t mq, sensor_msg_t *msg);
rt_err_t send_cmd_to_mq(rt_mq_t mq, command_t *cmd);

/* 通信系统初始化 */
int communication_init(void);
int mailbox_init(void);
/* 队列初始化 */
int queue_init(void);
/* 事件处理 */
void post_event(rt_uint32_t events);
rt_bool_t check_event(rt_uint32_t events);
rt_uint32_t wait_event(rt_uint32_t events, rt_int32_t timeout);
void send_command(rt_mailbox_t mailbox, command_t *cmd);
rt_bool_t receive_command(rt_mailbox_t mailbox, command_t *cmd, rt_int32_t timeout);

/* 数据保护 */
void lock_data(void);
void unlock_data(void);

/* 全局dete数据互斥锁 */
extern rt_mutex_t data_mutex;

extern rt_event_t system_events;

/* 全局邮箱声明 */
extern rt_mailbox_t atomiz_mailbox;
extern rt_mailbox_t fan_mailbox;
extern rt_mailbox_t pump_mailbox;
extern rt_mailbox_t led_mailbox;
extern rt_mailbox_t buzzer_mailbox;
/* 全局消息队列声明 */
extern rt_mq_t display_queue;
extern rt_mq_t upload_queue;
extern rt_mq_t fusion_queue;
extern rt_mq_t fan_queue;
extern rt_mq_t pump_queue;
extern rt_mq_t led_queue;
extern rt_mq_t buzzer_queue;
extern rt_mq_t atomiz_queue;


#endif /* APPLICATIONS_IPC_MANAGER_H_ */
