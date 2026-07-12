/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-02     17625       the first version
 */
#ifndef APPLICATIONS_ACTUATORS_H_
#define APPLICATIONS_ACTUATORS_H_

#include "common.h"

/* ==================== 执行器线程配置 ==================== */
// 风扇控制线程
#define THREAD_FAN_PRIORITY      7
#define THREAD_FAN_STACK_SIZE    1024
#define THREAD_FAN_TICK          100  // 100ms控制周期

// 水泵控制线程
#define THREAD_PUMP_PRIORITY     7
#define THREAD_PUMP_STACK_SIZE   1024
#define THREAD_PUMP_TICK         100  // 100ms控制周期

/* 加湿器线程配置 */
#define THREAD_ATOMIZ_PRIORITY     7
#define THREAD_ATOMIZ_STACK_SIZE   1024
#define THREAD_ATOMIZ_TICK         100  // 100ms控制周期

// LED控制线程
#define THREAD_LED_PRIORITY      7
#define THREAD_LED_STACK_SIZE    1024
#define THREAD_LED_TICK          50   // 50ms更新间隔

// 蜂鸣器控制线程
#define THREAD_BUZZER_PRIORITY   7
#define THREAD_BUZZER_STACK_SIZE 1280
#define THREAD_BUZZER_TICK       20   // 20ms更新间隔

/* ==================== 音频控制线程配置 ==================== */
#define THREAD_AUDIO_PRIORITY   7
#define THREAD_AUDIO_STACK_SIZE 768
#define THREAD_AUDIO_TICK       100   // 100ms控制周期

/* JQ8900 两线串口协议相关宏定义 */
#define JQ_CMD_START        0xAA    // 起始码
#define JQ_CMD_PLAY_TRACK   0x07    // 指定曲目播放
#define JQ_CMD_STOP         0x04    // 停止
#define JQ_CMD_SET_MODE     0x18    // 设置循环模式
#define JQ_CMD_LEN_PLAY     0x02    // 指定曲目数据长度
#define JQ_CMD_LEN_STOP     0x00    // 停止指令无数据
#define JQ_CMD_LEN_MODE     0x01    // 循环模式数据长度
#define JQ_CMD_PLAY         0x02    // 播放（从头开始）
#define JQ_CMD_PAUSE        0x03    // 暂停
#define JQ_CMD_SET_VOLUME   0x13    // 设置音量
#define JQ_CMD_SET_EQ       0x1A    // 设置 EQ

#define JQ_MODE_SINGLE_LOOP 0x01    // 单曲循环模式

/* 缓冲区大小（最大指令长度：起始码+指令类型+数据长度+最多2字节数据+校验和） */
#define JQ_CMD_BUF_SIZE     6



//蜂鸣器状态机状态定义
typedef enum {
    BUZZER_STATE_IDLE = 0,      // 空闲（关闭）
    BUZZER_STATE_BEEP_ON,       // 正在鸣响
    BUZZER_STATE_BEEP_OFF,      // 静音（等待下一次鸣响）
} buzzer_state_t;

typedef struct {
    buzzer_state_t state;       // 当前状态
    uint16_t beep_duration;     // 鸣响持续时间（毫秒）
    uint16_t interval_duration; // 静音间隔时间（毫秒）
    uint8_t remaining_beeps;    // 剩余鸣响次数（0 表示无限次或单次模式）
    rt_timer_t timer;           // 软件定时器，用于延时
} buzzer_ctrl_t;


/* 执行器初始化 */
int fan_control_init(void);
int pump_control_init(void);
int led_control_init(void);
int buzzer_control_init(void);
int atomiz_control_init(void);
int audio_control_init(void);

/* 线程管理 */
int actuators_threads_init(void);
void actuators_threads_stop(void);


/* 命令处理 */
void handle_fan_command(command_t *cmd);
void handle_pump_command(command_t *cmd);
void handle_led_command(command_t *cmd);
void handle_buzzer_command(command_t *cmd);
void handle_atomiz_command(command_t *cmd);
void handle_audio_command(command_t *cmd);

/* 控制函数 */
void fan_set_speed(int speed);
void fan_turn_on(void);
void fan_turn_off(void);

void pump_turn_on(void);
void pump_turn_off(void);

void led_set_brightness(int brightness);
void led_turn_on(void);
void led_turn_off(void);

void buzzer_turn_on(void);
void buzzer_turn_off(void);

void atomiz_turn_on(void);
void atomiz_turn_off(void);

void audio_play_single_loop(uint16_t track);
void audio_stop(void);

#endif /* APPLICATIONS_ACTUATORS_H_ */
