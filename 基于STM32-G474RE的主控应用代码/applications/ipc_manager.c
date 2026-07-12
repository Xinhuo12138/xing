/*
#include <ipc_manager.h>
#include <ipc.h>
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-05     17625       Communication management module
 */
#include <rtdbg.h>
#include "ipc_manager.h"

/* ==================== 消息队列系统 ==================== */
rt_mq_t display_queue = RT_NULL;    //显示队列
rt_mq_t upload_queue = RT_NULL;     //上传队列
rt_mq_t fusion_queue = RT_NULL;     //融合队列
/* 新增：执行器专用队列声明 */
rt_mq_t fan_queue = RT_NULL;
rt_mq_t pump_queue = RT_NULL;
rt_mq_t led_queue = RT_NULL;
rt_mq_t buzzer_queue = RT_NULL;
rt_mq_t atomiz_queue = RT_NULL;
rt_mq_t audio_queue = RT_NULL;

/* 全局通信对象定义 */
rt_mutex_t data_mutex = RT_NULL;            /* 数据互斥锁 - 保护共享的传感器数据结构访问 */

rt_event_t system_events = RT_NULL;         /* 系统事件集 - 线程间事件通知机制 */

/* 全局命令邮箱定义 */
rt_mailbox_t atomiz_mailbox = RT_NULL;              /* 命令邮箱 - 传递控制命令的轻量级队列 */
rt_mailbox_t fan_mailbox = RT_NULL;
rt_mailbox_t pump_mailbox = RT_NULL;
rt_mailbox_t led_mailbox = RT_NULL;
rt_mailbox_t buzzer_mailbox = RT_NULL;
rt_mailbox_t audio_mailbox = RT_NULL;

/* 命令内存池定义 */
static struct rt_mempool *cmd_mp = RT_NULL;


int queue_init(void)
{
    /* 1. 显示队列 - 需要所有传感器数据，实时性要求高 */
    display_queue = rt_mq_create("display_q",
        MSG_QUEUE_ITEM_SIZE,
        DISPLAY_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    /* 2. 上传队列 - 用于云平台上传，数据可容忍延迟 */
    upload_queue = rt_mq_create("upload_q",
        MSG_QUEUE_ITEM_SIZE,
        UPLOAD_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);
    /* 3. 融合队列 - 用于融合线程决策，数据可容忍延迟 */
    fusion_queue = rt_mq_create("fusion_q",
        MSG_QUEUE_ITEM_SIZE,
        FUSION_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    /* 3. 执行机控制队列 - 用于本地控制决策，实时性要求高 */
    fan_queue = rt_mq_create("fen_q",
        CMD_QUEUE_ITEM_SIZE,
        FAN_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    pump_queue = rt_mq_create("pump_q",
        CMD_QUEUE_ITEM_SIZE,
        PUMP_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    led_queue = rt_mq_create("led_q",
        CMD_QUEUE_ITEM_SIZE,
        LED_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    buzzer_queue = rt_mq_create("buzzer_q",
        CMD_QUEUE_ITEM_SIZE,
        BUZZER_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    atomiz_queue = rt_mq_create("atomiz_q",
        CMD_QUEUE_ITEM_SIZE,
        ATOMIZ_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    audio_queue = rt_mq_create("audio_q",
        CMD_QUEUE_ITEM_SIZE,
        AUDIO_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);

    if (display_queue == RT_NULL || upload_queue == RT_NULL ||
        fan_queue == RT_NULL || pump_queue == RT_NULL
        || led_queue == RT_NULL || buzzer_queue == RT_NULL ||
        atomiz_queue == RT_NULL || fusion_queue == RT_NULL
        || audio_queue == RT_NULL) {
        rt_mq_delete(display_queue);
        rt_mq_delete(upload_queue);
        rt_mq_delete(fan_queue);
        rt_mq_delete(pump_queue);
        rt_mq_delete(led_queue);
        rt_mq_delete(buzzer_queue);
        rt_mq_delete(atomiz_queue);
        rt_mq_delete(fusion_queue);
        rt_mq_delete(audio_queue);
        LOG_E("Failed to create display queue");
        return -RT_ERROR;
    }

    LOG_I("Three queues initialized: Display(%d), Upload(%d)",
        DISPLAY_QUEUE_SIZE, UPLOAD_QUEUE_SIZE);
    return RT_EOK;
}

int mailbox_init(void)
{
    /* 3. 创建命令邮箱 - 传递控制命令的轻量级队列 */
    fan_mailbox = rt_mb_create("fan_mailbox",
        MAILBOX_SIZE,        // 邮箱容量16个消息
        RT_IPC_FLAG_FIFO);

    pump_mailbox = rt_mb_create("pump_mailbox",
        MAILBOX_SIZE,        // 邮箱容量16个消息
        RT_IPC_FLAG_FIFO);

    led_mailbox = rt_mb_create("led_mailbox",
        MAILBOX_SIZE,        // 邮箱容量16个消息
        RT_IPC_FLAG_FIFO);

    buzzer_mailbox = rt_mb_create("buzzer_mailbox",
        MAILBOX_SIZE,        // 邮箱容量16个消息
        RT_IPC_FLAG_FIFO);

    atomiz_mailbox = rt_mb_create("atomiz_mailbox",
        MAILBOX_SIZE,        // 邮箱容量16个消息
        RT_IPC_FLAG_FIFO);

    audio_mailbox = rt_mb_create("audio_mailbox",
        MAILBOX_SIZE,        // 邮箱容量16个消息
        RT_IPC_FLAG_FIFO);

    if (fan_mailbox == RT_NULL || pump_mailbox == RT_NULL
        || led_mailbox == RT_NULL || buzzer_mailbox == RT_NULL
        || atomiz_mailbox == RT_NULL || audio_mailbox == RT_NULL)
    {
        LOG_E("Failed to create mailbox");
        rt_mb_delete(fan_mailbox);
        rt_mb_delete(pump_mailbox);
        rt_mb_delete(led_mailbox);
        rt_mb_delete(buzzer_mailbox);
        rt_mb_delete(atomiz_mailbox);
        rt_mb_delete(audio_mailbox);
        return -RT_ERROR;
    }

    return RT_EOK;

}


/* ==================== 通信管理初始化 ==================== */
int communication_init(void)
{
    LOG_I("Initializing communication system...");

    /* 1. 创建数据互斥锁 */
    data_mutex = rt_mutex_create("data_mutex", RT_IPC_FLAG_FIFO);
    if (data_mutex == RT_NULL)
    {
        LOG_E("Failed to create data mutex");
        return -RT_ERROR;
    }

    /* 2. 创建系统事件集 */
    system_events = rt_event_create("system_events", RT_IPC_FLAG_FIFO);
    if (system_events == RT_NULL)
    {
        LOG_E("Failed to create system events");
        rt_mutex_delete(data_mutex);
        return -RT_ERROR;
    }
    /* 3. 创建命令内存池 */
    cmd_mp = rt_mp_create("cmd_mp", CMD_POOL_ITEM_SIZE, CMD_POOL_MAX_CNT);
    if (cmd_mp == RT_NULL) {
        LOG_E("Failed to create command memory pool");
        return -RT_ERROR;
    }

    LOG_I("Communication system initialized successfully");
    return RT_EOK;
}



/* ==================== 事件处理函数 ==================== */
void post_event(rt_uint32_t events)
{
    if (system_events != RT_NULL)
    {
        rt_event_send(system_events, events);
    }
}

rt_bool_t check_event(rt_uint32_t events)
{
    if (system_events != RT_NULL)
    {
        rt_uint32_t recved_events;
        return rt_event_recv(system_events,
            events,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            0,
            &recved_events) == RT_EOK;
    }
    return RT_FALSE;
}

rt_uint32_t wait_event(rt_uint32_t events, rt_int32_t timeout)
{
    rt_uint32_t recved_events;
    if (system_events != RT_NULL)
    {
        rt_err_t result = rt_event_recv(system_events,
            events,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            timeout,
            &recved_events);
        if (result == RT_EOK)
        {
            return recved_events;
        }
    }
    return 0;
}

/* ==================== 消息发送函数 ==================== */

/* 发送函数 - 根据策略处理队列满的情况 */
rt_err_t send_to_mq(rt_mq_t mq, sensor_msg_t *msg)
{
    if (mq == RT_NULL || msg == RT_NULL)
        return -RT_ERROR;

    rt_err_t result;

    /* 丢弃最旧数据：如果队列满，先接收一个消息再发送 */
    result = rt_mq_send(mq, msg, MSG_QUEUE_ITEM_SIZE);
    if (result == -RT_EFULL) {
        sensor_msg_t temp_msg;
        rt_mq_recv(mq, &temp_msg, MSG_QUEUE_ITEM_SIZE, 0);
        result = rt_mq_send(mq, msg, MSG_QUEUE_ITEM_SIZE);
        if (result != RT_EOK) {
            LOG_W("send_to_mq: after drop still failed, type=%d", msg->type);
        }
    }else if (result != RT_EOK) {
        LOG_E("send_to_mq: rt_mq_send error %d, type=%d", result, msg->type);
    }

    return result;
}

rt_err_t send_cmd_to_mq(rt_mq_t mq, command_t *cmd)
{
    if (mq == RT_NULL || cmd == RT_NULL)
        return -RT_ERROR;

    rt_err_t result;
    /* 丢弃最旧数据：如果队列满，先接收一个命令再发送 */
    result = rt_mq_send(mq, cmd, sizeof(command_t));
    if (result == -RT_EFULL) {
        command_t temp_cmd;
        rt_mq_recv(mq, &temp_cmd, sizeof(command_t), 0);
        result = rt_mq_send(mq, cmd, sizeof(command_t));
    }
    return result;
}

/* ==================== 命令传递函数 ==================== */
void send_command(rt_mailbox_t mailbox, command_t *cmd)
{
    if (mailbox == RT_NULL || cmd == RT_NULL)
        return;

    // 从内存池中分配命令副本
    command_t *cmd_copy = (command_t *)rt_mp_alloc(cmd_mp, RT_WAITING_FOREVER);
    if (cmd_copy == RT_NULL) {
        LOG_E("Failed to allocate command from memory pool");
        return;
    }

    // 复制命令数据
    rt_memcpy(cmd_copy, cmd, sizeof(command_t));
    cmd_copy->timestamp = rt_tick_get();

    rt_err_t result = rt_mb_send(mailbox, (rt_uint32_t)cmd_copy);
    if (result != RT_EOK)
    {
        LOG_W("Failed to send command to mailbox");
        rt_mp_free(cmd_copy);   // 发送失败则释放回内存池
    }
}

rt_bool_t receive_command(rt_mailbox_t mailbox, command_t *cmd, rt_int32_t timeout)
{
    if (mailbox == RT_NULL || cmd == RT_NULL)
          return RT_FALSE;

      rt_uint32_t received_msg;
      rt_err_t result = rt_mb_recv(mailbox, &received_msg, timeout);

      if (result == RT_EOK)
      {
          command_t *temp_cmd = (command_t *)received_msg;

          // 验证指针有效性
          if (temp_cmd == RT_NULL) {
              LOG_E("Received NULL command pointer");
              return RT_FALSE;
          }

          // 复制命令数据
          rt_memcpy(cmd, temp_cmd, sizeof(command_t));

          rt_mp_free(temp_cmd);   // 归还内存池

          LOG_D("Received command: type=%d, timestamp=%u", cmd->type, cmd->timestamp);
          return RT_TRUE;
      }
      else if (result == -RT_ETIMEOUT && timeout != RT_WAITING_FOREVER)
      {
          // 超时是正常情况，不记录为错误
          return RT_FALSE;
      }
      else if (result != RT_EOK)
      {
          LOG_W("Failed to receive command from mailbox 0x%08x, error: %d",
              (rt_uint32_t)mailbox, result);
      }
      return RT_FALSE;
}

/* ==================== 数据保护函数 ==================== */
void lock_data(void)
{
    if (data_mutex != RT_NULL)
    {
        rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    } else {
        LOG_E("lock_data: data_mutex is NULL");
    }
}

void unlock_data(void)
{
    if (data_mutex != RT_NULL)
    {
        rt_mutex_release(data_mutex);
    } else {
        LOG_E("lock_data: data_mutex is NULL");
    }
}


