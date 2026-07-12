/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-05     17625       Updated with thread support
 */
#include "ipc_manager.h"
#include "actuators.h"
#include "common.h"
#include <rtdbg.h>
#include <math.h>
#include <stdlib.h>      /* for rand() */


/* ==================== 风扇控制配置 ==================== */
#define FAN_PWM_DEVICE "pwm2"
#define FAN_PWM_CHANNEL  1
#define FAN_DEFAULT_SPEED 100  // 默认风扇速度 (100-50)%

/* ==================== 蜂鸣器控制配置 ==================== */
#define BUZZER_PIN GET_PIN(A, 6)  // 假设使用PA6作为蜂鸣器控制引脚，低电平触发
static buzzer_ctrl_t buzzer_ctrl; //存储内部时序控制所需的信息

/* ==================== 水泵控制配置 ==================== */
#define PUMP_PIN GET_PIN(A, 7)  // 假设使用PA7作为水泵控制引脚

/* ==================== 加湿器控制配置 ==================== */
#define ATOMIZ_PIN GET_PIN(D, 2)  // 假设使用PD2作为加湿器控制引脚

/* ==================== LED呼吸灯配置 ==================== */
#define LED_PWM_DEVICE "pwm5"
#define LED_PWM_CHANNEL 2
#define M_PI 3.14159265358979323846

/* ==================== 音频播放配置 ==================== */
#define AUDIO_UART_NAME       "uart4"

static struct rt_device_pwm *fan_pwm_dev = RT_NULL;
static struct rt_device_pwm *led_pwm_dev = RT_NULL;
static rt_device_t audio_uart = RT_NULL;

/* 全局执行器控制标志 */
static rt_bool_t actuators_running = RT_FALSE;
static rt_thread_t fan_thread = RT_NULL;
static rt_thread_t pump_thread = RT_NULL;
static rt_thread_t atomiz_thread = RT_NULL;
static rt_thread_t led_thread = RT_NULL;
static rt_thread_t buzzer_thread = RT_NULL;
static rt_thread_t audio_thread = RT_NULL;


static void buzzer_control_thread_entry(void *parameter);
static void led_control_thread_entry(void *parameter);
static void pump_control_thread_entry(void *parameter);
static void fan_control_thread_entry(void *parameter);
static void atomiz_control_thread_entry(void *parameter);
static void audio_control_thread_entry(void *parameter);
/* ==================== 蜂鸣器辅助函数（前置声明） ==================== */
void buzzer_state_machine_init(void);
void buzzer_timer_callback(void *parameter);
void buzzer_start_alarm(uint16_t beep_ms, uint16_t interval_ms, uint8_t count);

/* 发送执行器状态到上传队列 */
static void send_actuator_update(sensor_type_t type, int32_t mode, rt_bool_t state, int32_t value)
{
    sensor_msg_t msg;
    msg.type = type;
    msg.timestamp = rt_tick_get();
    msg.data.actor.work_mode = mode;
    msg.data.actor.state = state;
    msg.data.actor.value = value;
    send_to_mq(upload_queue, &msg);
    LOG_I("send_to display_queue");
    send_to_mq(display_queue, &msg);

}

/* ==================== 执行器线程函数 ==================== */
static void fan_control_thread_entry(void *parameter)
{
    LOG_I("Fan control thread started");

    while (actuators_running)
    {
        /* 检查是否有来自命令邮箱的控制命令 */
        command_t cmd;
        if (receive_command(fan_mailbox, &cmd, 0))// 非阻塞接收命令（timeout=0）
        {
            if (cmd.type == MSG_FAN || cmd.type == MSG_ALL)
            {
                handle_fan_command(&cmd);
            }
            LOG_I("Fan rece cmd");
        }

        /* 执行自动控制 */
        if (sensor_data.fan.mode == FAN_AUTO)
        {
            command_t cmd;
            /* 从控制队列读取传感器数据 */
            rt_ssize_t  result = rt_mq_recv(fan_queue, &cmd,
                sizeof(command_t), 0);


            if (result >= 0)
            {
                LOG_D("[FAN] Auto cmd: status=%d, speed=%d", cmd.data.fan.fan_status, cmd.data.fan.speed);
                if(cmd.type == MSG_FAN ||cmd.source == INTER)
                {
                    if (cmd.data.fan.fan_status == RT_TRUE) {
                        fan_set_speed(cmd.data.fan.speed);
                    }else {
                        fan_turn_off();
                    }
                }

            }
        }

        rt_thread_mdelay(THREAD_FAN_TICK);
    }
}

static void pump_control_thread_entry(void *parameter)
{
    LOG_I("Pump control thread started");

    while (actuators_running)
    {
        /* 检查命令 */
        command_t cmd;
        if (receive_command(pump_mailbox, &cmd, 0))// 非阻塞接收命令（timeout=0）
        {
            if (cmd.type == MSG_PUMP || cmd.type == MSG_ALL)
            {
                handle_pump_command(&cmd);
            }
        }

        /* 执行自动控制 */
        if (sensor_data.pump.mode == PUMP_AUTO)
        {
            command_t cmd;
            /* 从控制队列读取传感器数据 */
            rt_ssize_t result = rt_mq_recv(pump_queue, &cmd,
                sizeof(command_t), 0);

            if (result >= 0)
            {
                LOG_D("[PUM] Auto cmd: status=%d", cmd.data.pump.pump_status);
                if(cmd.type == MSG_PUMP ||cmd.source == INTER)
                {
                    if (cmd.data.pump.pump_status == RT_TRUE) {
                        pump_turn_on();
                    }else {
                        pump_turn_off();
                    }
                }

            }
        }

        rt_thread_mdelay(THREAD_PUMP_TICK);
    }
}

static void led_control_thread_entry(void *parameter)
{
    LOG_I("LED control thread started");

    while (actuators_running)
    {
        /* 检查命令 */
        command_t cmd;
        if (receive_command(led_mailbox, &cmd, 0))// 非阻塞接收命令（timeout=0）
        {
            if (cmd.type == MSG_LED || cmd.type == MSG_ALL)
            {
                handle_led_command(&cmd);
            }
        }

        /* 执行自动控制 */
        if (sensor_data.led.mode == LED_AUTO)
        {
            command_t cmd;
            /* 从控制队列读取传感器数据 */
            rt_ssize_t  result = rt_mq_recv(led_queue, &cmd,
                sizeof(command_t), 0);


            if (result >= 0)
            {
                LOG_D("[LED] Auto cmd: status=%d, brightness=%d", cmd.data.led.led_status, cmd.data.led.brightness);
                if(cmd.type == MSG_LED ||cmd.source == INTER)
                {
                    if (cmd.data.led.led_status == RT_TRUE) {
                        led_set_brightness(cmd.data.led.brightness);
                    }else {
                        led_turn_off();
                    }
                }

            }
        }

        rt_thread_mdelay(THREAD_LED_TICK);
    }
}

static void buzzer_control_thread_entry(void *parameter)
{
    LOG_I("Buzzer control thread started");

    while (actuators_running)
    {
        /* 检查命令和事件 */
        static command_t cmd;
        if (receive_command(buzzer_mailbox, &cmd, 0))// 非阻塞接收命令（timeout=0）
        {
            if (cmd.type == MSG_BUZZER || cmd.type == MSG_ALL)
            {
                handle_buzzer_command(&cmd);
            }
        }

        /* 2. 自动模式：等待事件（报警、清除、定时器超时） */
        if (sensor_data.buzzer.mode == BUZZER_AUTO)
        {
            rt_uint32_t recv_events;
            /* 只等待三个事件：统一报警、清除、定时器超时 */
            rt_err_t res = rt_event_recv(system_events,
                                         EVENT_ALARM_GENERAL | EVENT_ALARM_ALL_CLEAR | EVENT_BUZZER_TIMEOUT,
                                         RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                         0,   /* 非阻塞等待，不超时 */
                                         &recv_events);

            if (res == RT_EOK)
            {
                LOG_D("buzzer_thread: events received = 0x%08X", recv_events);
                if (recv_events & EVENT_ALARM_ALL_CLEAR)
                {
                    LOG_I("Alarm cleared, stopping buzzer");
                    /* 清除所有报警：停止当前蜂鸣序列 */
                    if (buzzer_ctrl.timer && buzzer_ctrl.timer->parent.flag & RT_TIMER_FLAG_ACTIVATED) {
                        rt_timer_stop(buzzer_ctrl.timer);
                    }
                    buzzer_turn_off();
                    buzzer_ctrl.state = BUZZER_STATE_IDLE;
                    buzzer_ctrl.remaining_beeps = 0;
                    LOG_I("Alarm cleared, buzzer stopped");
                    rt_event_recv(system_events, EVENT_BUZZER_TIMEOUT, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, NULL);
                }
                /* 处理定时器超时（状态机转移） */
                else if (recv_events & EVENT_BUZZER_TIMEOUT)
                {
                    LOG_D("buzzer_state_machine: timeout event, current state=%d", buzzer_ctrl.state);
                    switch (buzzer_ctrl.state)
                    {
                    case BUZZER_STATE_BEEP_ON:
                        LOG_D("buzzer_state_machine: BEEP_ON -> turn off");
                        buzzer_turn_off();
                        if (buzzer_ctrl.remaining_beeps > 1) {
                            buzzer_ctrl.state = BUZZER_STATE_BEEP_OFF;
                            rt_tick_t interval_tick = rt_tick_from_millisecond(buzzer_ctrl.interval_duration);
                            rt_timer_control(buzzer_ctrl.timer, RT_TIMER_CTRL_SET_TIME, &interval_tick);
                            rt_timer_start(buzzer_ctrl.timer);
                            buzzer_ctrl.remaining_beeps--;
                        } else if (buzzer_ctrl.remaining_beeps == 1) {
                            buzzer_ctrl.state = BUZZER_STATE_IDLE;
                            buzzer_ctrl.remaining_beeps = 0;
                        } else { // remaining_beeps == 0 无限循环模式
                            buzzer_ctrl.state = BUZZER_STATE_BEEP_OFF;
                            rt_tick_t interval_tick = rt_tick_from_millisecond(buzzer_ctrl.interval_duration);
                            rt_timer_control(buzzer_ctrl.timer, RT_TIMER_CTRL_SET_TIME, &interval_tick);
                            rt_timer_start(buzzer_ctrl.timer);
                            LOG_D("buzzer_state_machine: -> BEEP_OFF (infinite loop)");
                        }
                        break;

                    case BUZZER_STATE_BEEP_OFF:
                        buzzer_ctrl.state = BUZZER_STATE_BEEP_ON;
                        buzzer_turn_on();
                        rt_tick_t beep_tick = rt_tick_from_millisecond(buzzer_ctrl.beep_duration);
                        rt_timer_control(buzzer_ctrl.timer, RT_TIMER_CTRL_SET_TIME, &beep_tick);
                        rt_timer_start(buzzer_ctrl.timer);
                        LOG_D("buzzer_state_machine: BEEP_OFF -> BEEP_ON, beep_tick=%d", beep_tick);
                        break;

                    default:
                        break;
                    }
                }
                else if (recv_events & EVENT_ALARM_GENERAL)
                {
                    rt_event_recv(system_events, EVENT_BUZZER_TIMEOUT, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, NULL);
                    LOG_D("buzzer_thread: general alarm triggered");
                    /* 停止当前可能正在进行的蜂鸣 */
                    if (buzzer_ctrl.timer && (buzzer_ctrl.timer->parent.flag & RT_TIMER_FLAG_ACTIVATED))
                        rt_timer_stop(buzzer_ctrl.timer);
                    buzzer_turn_off();
                    /* 固定参数：响200ms，停300ms，重复5次（可调整） */
                    buzzer_start_alarm(200, 300, 5);
                }
            }
        }
        rt_thread_mdelay(THREAD_BUZZER_TICK);
    }
}


/* 加湿器控制线程入口函数 */
static void atomiz_control_thread_entry(void *parameter)
{
    LOG_I("Atomizer control thread started");

    while (actuators_running)
    {
        /* 检查命令 */
        command_t cmd;
        if (receive_command(atomiz_mailbox, &cmd, 0)) // 非阻塞接收命令
        {
            if (cmd.type == MSG_ATOMIZ || cmd.type == MSG_ALL)
            {
                handle_atomiz_command(&cmd);
            }
        }

        /* 执行自动控制 */
        if (sensor_data.atomiz.mode == ATOMIZ_AUTO)
        {
            command_t cmd;
            /* 从控制队列读取传感器数据 */
            rt_ssize_t  result = rt_mq_recv(atomiz_queue, &cmd,
                sizeof(command_t), 0);


            if (result >= 0)
            {
                LOG_D("[ATO] Auto cmd: status=%d", cmd.data.atomiz.atomiz_status);
                if(cmd.type == MSG_ATOMIZ ||cmd.source == INTER)
                {
                    if (cmd.data.atomiz.atomiz_status == RT_TRUE) {
                        atomiz_turn_on();
                    }else {
                        atomiz_turn_off();
                    }
                }
            }
        }

        rt_thread_mdelay(THREAD_ATOMIZ_TICK);
    }
}

static void audio_control_thread_entry(void *parameter)
{
    LOG_I("Audio control thread started");

    while (actuators_running)
    {
        command_t cmd;

        /* 检查命令邮箱（手动控制） */
        if (receive_command(audio_mailbox, &cmd, 0))
        {
            if (cmd.type == MSG_AUDIO || cmd.type == MSG_ALL)
            {
                handle_audio_command(&cmd);
            }
        }

        /* 自动模式：从队列接收融合线程指令 */
        if (sensor_data.audio.mode == AUDIO_AUTO)
        {
            command_t cmd;
            /* 从控制队列读取传感器数据 */
            rt_ssize_t  result = rt_mq_recv(audio_queue, &cmd,
                sizeof(command_t), 0);

            if (result >= 0)
            {
                LOG_D("[AUD] Auto cmd: status=%d", cmd.data.audio.audio_status);
                if(cmd.type == MSG_AUDIO ||cmd.source == INTER)
                {
                    if (cmd.data.audio.audio_status == RT_TRUE) {
                        audio_play_single_loop(1);
                    }else {
                        audio_stop();
                    }
                }
            }
        }

        rt_thread_mdelay(THREAD_AUDIO_TICK);
    }
}
/* ==================== 命令处理函数 ==================== */
void handle_fan_command(command_t *cmd)
{
    if (cmd == RT_NULL) return;

    lock_data();
    sensor_data.fan.mode = cmd->data.fan.mode;
    unlock_data();

    if(cmd->data.fan.mode == FAN_MANU)
    {
        if(cmd->data.fan.fan_status){
            fan_set_speed(cmd->data.fan.speed);
        }
        else{
            fan_turn_off();
        }
    }else {
        /* 上传模式变化 */
        send_actuator_update(ACTUATOR_FAN, sensor_data.fan.mode,
                             sensor_data.fan.current_status, sensor_data.fan.speed);
    }
}

void handle_pump_command(command_t *cmd)
{
    if (cmd == RT_NULL) return;

    lock_data();
    sensor_data.pump.mode = cmd->data.pump.mode;
    unlock_data();

    if(cmd->data.pump.mode == PUMP_MANU)
    {
        if(cmd->data.pump.pump_status){
            pump_turn_on();
        }
        else{
            pump_turn_off();
        }
    } else {
        send_actuator_update(ACTUATOR_PUMP, sensor_data.pump.mode, sensor_data.pump.current_status, 0);
    }
}

void handle_led_command(command_t *cmd)
{
    if (cmd == RT_NULL) return;

    lock_data();
    sensor_data.led.mode = cmd->data.led.mode;
    unlock_data();

    if(cmd->data.led.mode == LED_MANU)
    {
        if(cmd->data.led.led_status){
            led_set_brightness(cmd->data.led.brightness);
        }
        else{
            led_turn_off();
        }
    } else {
        send_actuator_update(ACTUATOR_LED, sensor_data.led.mode,sensor_data.led.current_status,sensor_data.led.brightness);
    }
}

void handle_buzzer_command(command_t *cmd)
{
    if (cmd == RT_NULL) return;

    lock_data();
    buzzer_mode_t old_mode = sensor_data.buzzer.mode;
    sensor_data.buzzer.mode = cmd->data.buzzer.mode;
    unlock_data();

    /* 1. 停止定时器（无论模式） */
    if (buzzer_ctrl.timer != RT_NULL &&
        (buzzer_ctrl.timer->parent.flag & RT_TIMER_FLAG_ACTIVATED)) {
        rt_timer_stop(buzzer_ctrl.timer);
    }

    if(cmd->data.buzzer.mode == BUZZER_MANU)
    {
        if(cmd->data.buzzer.buzzer_status){
            buzzer_turn_on();
        }
        else{
            buzzer_turn_off();
        }
    } else {
        // 从手动切换到自动时，关闭当前蜂鸣并复位状态机
        if (old_mode == BUZZER_MANU)
        {
            rt_event_recv(system_events, EVENT_BUZZER_TIMEOUT, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, NULL);
            buzzer_turn_off();
            buzzer_ctrl.state = BUZZER_STATE_IDLE;
            buzzer_ctrl.remaining_beeps = 0;
        }
        send_actuator_update(ACTUATOR_BUZZER, sensor_data.buzzer.mode,
                             sensor_data.buzzer.current_status, 0);
    }
}
/* ==================== 加湿器命令处理函数 ==================== */
void handle_atomiz_command(command_t *cmd)
{
    if (cmd == RT_NULL) return;

    lock_data();
    sensor_data.atomiz.mode = cmd->data.atomiz.mode;
    unlock_data();

    if(cmd->data.atomiz.mode == ATOMIZ_MANU)
    {
        if (cmd->data.atomiz.atomiz_status){
            atomiz_turn_on();
        }
        else{
            atomiz_turn_off();
        }
    } else {
        send_actuator_update(ACTUATOR_ATOMIZ, sensor_data.atomiz.mode, sensor_data.atomiz.current_status, 0);
    }
}

/* ==================== 音频命令处理函数 ==================== */
void handle_audio_command(command_t *cmd)
{
    if (cmd == RT_NULL) return;

    lock_data();
    sensor_data.audio.mode = cmd->data.audio.mode;
    unlock_data();

    if(cmd->data.audio.mode == AUDIO_MANU)
    {
        if (cmd->data.audio.audio_status){
            audio_play_single_loop(1);
            LOG_I("Audio: start playing track 1 (loop)");
        }
        else{
            audio_stop();
            LOG_I("Audio: stop");
        }
    }else {
        send_actuator_update(ACTUATOR_AUDIO, sensor_data.audio.mode, sensor_data.audio.current_status, 0);
    }
}
/* ==================== 硬件初始化函数 ==================== */
int fan_control_init(void)
{
    /* 初始化PWM设备 */
    fan_pwm_dev = (struct rt_device_pwm *)rt_device_find(FAN_PWM_DEVICE);
    if (fan_pwm_dev == RT_NULL)
    {
        LOG_E("PWM device %s not found!", FAN_PWM_DEVICE);
        return -RT_ERROR;
    }

    /* 设置PWM参数 */
    rt_uint32_t period = 1000000;  // 1MHz, 1us周期
    rt_uint32_t pulse = 0;       // 初始比较值为0

    if (rt_pwm_set(fan_pwm_dev, FAN_PWM_CHANNEL, period, pulse) != RT_EOK)
        return -RT_ERROR;


    if (rt_pwm_enable(fan_pwm_dev, FAN_PWM_CHANNEL) != RT_EOK)
        return -RT_ERROR;


    /* 初始化数据结构 */
    lock_data();
    sensor_data.fan.mode = FAN_MANU;
    sensor_data.fan.current_status = RT_FALSE;
    sensor_data.fan.speed = FAN_DEFAULT_SPEED;
    unlock_data();

    fan_turn_off();
    /* 上传模式变化 */
    LOG_I("Fan control initialized");
    return RT_EOK;
}

int led_control_init(void)
{
    /* 初始化PWM设备 */
    led_pwm_dev = (struct rt_device_pwm *)rt_device_find(LED_PWM_DEVICE);
    if (led_pwm_dev == RT_NULL)
    {
        LOG_E("PWM device %s not found!", LED_PWM_DEVICE);
        return -RT_ERROR;
    }

    /* 设置PWM参数 */
    rt_uint32_t period = 1000000;  // 1MHz, 1us周期
    rt_uint32_t pulse = 0;         // 初始占空比为0

    if (rt_pwm_set(led_pwm_dev, LED_PWM_CHANNEL, period, pulse) != RT_EOK)
        return -RT_ERROR;


    if (rt_pwm_enable(led_pwm_dev, LED_PWM_CHANNEL) != RT_EOK)
        return -RT_ERROR;


    /* 初始化数据结构 */
    lock_data();
    sensor_data.led.mode = LED_MANU;        // 修改：手动模式
    sensor_data.led.brightness = 0;        // 修改：默认亮度50%
    sensor_data.led.current_status = RT_FALSE; // 修改：默认开启
    unlock_data();

    led_turn_off();

    LOG_I("LED control initialized");
    return RT_EOK;
}

int pump_control_init(void)
{
    /* 初始化GPIO */
    rt_pin_mode(PUMP_PIN, PIN_MODE_OUTPUT);


    /* 初始化数据结构 */
    lock_data();
    sensor_data.pump.mode = PUMP_MANU;
    sensor_data.pump.current_status = RT_FALSE;// RT_TRUE;
    unlock_data();

    pump_turn_off();

    LOG_I("Pump control initialized");
    return RT_EOK;
}


int buzzer_control_init(void)
{
    /* 初始化GPIO为输出模式 */
    rt_pin_mode(BUZZER_PIN, PIN_MODE_OUTPUT);

    /* 初始设置为高电平，蜂鸣器不响 */
    buzzer_turn_off();

    /* 初始化数据结构 */
    lock_data();
    sensor_data.buzzer.mode = BUZZER_MANU;
    sensor_data.buzzer.current_status = RT_FALSE;
    unlock_data();

    /* 初始化状态机 */
    buzzer_state_machine_init();

    /* 初始设置为高电平，蜂鸣器不响 */
    buzzer_turn_off();

    LOG_I("Buzzer control initialized");
    return RT_EOK;
}

/* 初始化音频串口 */
int audio_control_init(void)
{
    // 查找 UART 设备
    audio_uart = rt_device_find(AUDIO_UART_NAME);
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART device %s not found", AUDIO_UART_NAME);
        return -RT_ERROR;
    }

    // 打开设备（读写模式）
    if (rt_device_open(audio_uart, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        LOG_E("Failed to open audio UART device %s", AUDIO_UART_NAME);
        return -RT_ERROR;
    }

    // 配置串口参数：9600bps, 8数据位, 1停止位, 无校验
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = 9600;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity = PARITY_NONE;
    if (rt_device_control(audio_uart, RT_DEVICE_CTRL_CONFIG, &config) != RT_EOK) {
        LOG_E("Failed to configure audio UART");
        rt_device_close(audio_uart);
        audio_uart = RT_NULL;
        return -RT_ERROR;
    }

    LOG_I("Audio UART %s initialized, baudrate=%d", AUDIO_UART_NAME, config.baud_rate);

    /* 初始化数据结构（音频状态） */
    lock_data();
    sensor_data.audio.mode = AUDIO_MANU;
    sensor_data.audio.current_status = RT_FALSE;
    unlock_data();

    // 可选：发送一次停止指令，确保模块处于空闲状态
    audio_stop();

    return RT_EOK;
}

int atomiz_control_init(void)
{
    /* 初始化GPIO */
    rt_pin_mode(ATOMIZ_PIN, PIN_MODE_OUTPUT);
    atomiz_turn_off();

    /* 初始化数据结构 */
    lock_data();
    sensor_data.atomiz.mode = ATOMIZ_MANU;
    sensor_data.atomiz.current_status = RT_FALSE;
    unlock_data();

    atomiz_turn_off();

    LOG_I("Atomizer control initialized");
    return RT_EOK;
}


/* ==================== 执行器线程管理 ==================== */
int actuators_threads_init(void)
{

    actuators_running = RT_TRUE;

    if (fan_control_init() != RT_EOK)
    {
        LOG_E("Failed to init fan PWM");
    }

    if (pump_control_init() != RT_EOK)
    {
        LOG_E("Failed to init pump GPIO");
    }

    if (led_control_init() != RT_EOK)
    {
        LOG_E("Failed to init led PWM");
    }

    if (buzzer_control_init() != RT_EOK)
    {
        LOG_E("Failed to init buzzer GPIO");
    }

    if (atomiz_control_init() != RT_EOK)
    {
        LOG_E("Failed to init atomiz GPIO");
    }

    /* 初始化音频串口 */
    if (audio_control_init() != RT_EOK)
    {
        LOG_E("Failed to init audio uart");
    }

    /* 创建风扇控制线程 */
    fan_thread = rt_thread_create("fan_ctrl",
        fan_control_thread_entry,
        RT_NULL,
        THREAD_FAN_STACK_SIZE,
        THREAD_FAN_PRIORITY,
        10);
    if (fan_thread == RT_NULL)
    {
        LOG_E("Failed to create fan control thread");
    }

    /* 创建水泵控制线程 */
    pump_thread = rt_thread_create("pump_ctrl",
        pump_control_thread_entry,
        RT_NULL,
        THREAD_PUMP_STACK_SIZE,
        THREAD_PUMP_PRIORITY,
        10);
    if (pump_thread == RT_NULL)
    {
        LOG_E("Failed to create pump control thread");
    }

    /* 创建LED控制线程 */
    led_thread = rt_thread_create("led_ctrl",
        led_control_thread_entry,
        RT_NULL,
        THREAD_LED_STACK_SIZE,
        THREAD_LED_PRIORITY,
        10);
    if (led_thread == RT_NULL)
    {
        LOG_E("Failed to create LED control thread");
    }

    /* 创建蜂鸣器控制线程 */
    buzzer_thread = rt_thread_create("buzzer_ctrl",
        buzzer_control_thread_entry,
        RT_NULL,
        THREAD_BUZZER_STACK_SIZE,
        THREAD_BUZZER_PRIORITY,
        10);
    if (buzzer_thread == RT_NULL)
    {
        LOG_E("Failed to create buzzer control thread");
    }

    /* 创建加湿器控制线程 */
    atomiz_thread = rt_thread_create("atomiz_ctrl",
        atomiz_control_thread_entry,
        RT_NULL,
        THREAD_ATOMIZ_STACK_SIZE,
        THREAD_ATOMIZ_PRIORITY,
        10);
    if (atomiz_thread == RT_NULL)
    {
        LOG_E("Failed to create atomiz control thread");
    }

    /* 创建音频混合线程 */
    audio_thread = rt_thread_create("audio_ctrl",
        audio_control_thread_entry,
        RT_NULL,
        THREAD_AUDIO_STACK_SIZE,
        THREAD_AUDIO_PRIORITY,
        10);
    if (audio_thread == RT_NULL)
    {
        LOG_E("Failed to create audio thread");
    }


    if (fan_thread == RT_NULL || pump_thread == RT_NULL
            || led_thread == RT_NULL || atomiz_thread == RT_NULL
            || buzzer_thread == RT_NULL || audio_thread == RT_NULL
            )
    {
        actuators_threads_stop();
        return -RT_ERROR;
    }


    /* 启动所有线程 */
    rt_thread_startup(fan_thread);
    rt_thread_startup(pump_thread);
    rt_thread_startup(led_thread);
    rt_thread_startup(buzzer_thread);
    rt_thread_startup(atomiz_thread);
    rt_thread_startup(audio_thread);
    LOG_I("All actuators control threads started successfully");
    return RT_EOK;
}

/*停止和清理所有执行器控制线程*/
void actuators_threads_stop(void)
{
    actuators_running = RT_FALSE;

    /* 等待线程结束 */
    rt_thread_mdelay(500);

    /* 清理线程资源 */
    if (fan_thread != RT_NULL)
    {
        rt_thread_delete(fan_thread);
        fan_thread = RT_NULL;
    }

    /* 清理加湿器线程资源 */
    if (atomiz_thread != RT_NULL)
    {
        rt_thread_delete(atomiz_thread);
        atomiz_thread = RT_NULL;
    }

    if (pump_thread != RT_NULL)
    {
        rt_thread_delete(pump_thread);
        pump_thread = RT_NULL;
    }

    if (led_thread != RT_NULL)
    {
        rt_thread_delete(led_thread);
        led_thread = RT_NULL;
    }

    if (buzzer_thread != RT_NULL)
    {
        rt_thread_delete(buzzer_thread);
        buzzer_thread = RT_NULL;
    }

    if (audio_thread != RT_NULL)
    {
        rt_thread_delete(audio_thread);
        audio_thread = RT_NULL;
    }

    /* 关闭音频串口设备 */
    if (audio_uart != RT_NULL)
    {
        rt_device_close(audio_uart);
        audio_uart = RT_NULL;
    }

    LOG_I("Actuators threads stopped");
}

/* ==================== 控制函数实现 ==================== */
void fan_set_speed(int speed)
{
    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;

    if (fan_pwm_dev != RT_NULL)
    {
        rt_uint32_t period = 1000000;
        rt_uint32_t pulse = (period * (100 - speed)) / 100;

        if (rt_pwm_set(fan_pwm_dev, FAN_PWM_CHANNEL, period, pulse) == RT_EOK)
        {
            lock_data();
            sensor_data.fan.speed = speed;
            sensor_data.fan.current_status = (speed > 0);
            unlock_data();

            /* 上传状态 */
            send_actuator_update(ACTUATOR_FAN, sensor_data.fan.mode,
                                 sensor_data.fan.current_status, speed);
        }
    }
}

void fan_turn_on(void)
{
    lock_data();
    int speed = sensor_data.fan.speed;
    unlock_data();
    fan_set_speed(speed);
}

void fan_turn_off(void)
{
    fan_set_speed(0);
}

void pump_turn_on(void)
{
    rt_pin_write(PUMP_PIN, PIN_HIGH);
    lock_data();
    sensor_data.pump.current_status = RT_TRUE;
    unlock_data();
    send_actuator_update(ACTUATOR_PUMP, sensor_data.pump.mode, RT_TRUE, 0);
}

void pump_turn_off(void)
{
    rt_pin_write(PUMP_PIN, PIN_LOW);
    lock_data();
    sensor_data.pump.current_status = RT_FALSE;
    unlock_data();
    send_actuator_update(ACTUATOR_PUMP, sensor_data.pump.mode, RT_FALSE, 0);
}

void led_set_brightness(int brightness)
{
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;

    if (led_pwm_dev != RT_NULL)
    {
        rt_uint32_t period = 1000000;
        rt_uint32_t pulse = (period * brightness) / 100;

        if (rt_pwm_set(led_pwm_dev, LED_PWM_CHANNEL, period, pulse) == RT_EOK)
        {
            lock_data();
            sensor_data.led.brightness = brightness;
            sensor_data.led.current_status = (brightness > 0);
            unlock_data();

            send_actuator_update(ACTUATOR_LED, sensor_data.led.mode,
                                 sensor_data.led.current_status, brightness);
        }

    }
}

void led_turn_on(void)
{
    lock_data();
    int brightness = sensor_data.led.brightness;
    unlock_data();
    led_set_brightness(brightness);
}

void led_turn_off(void)
{
    led_set_brightness(0);
}



/* ==================== 加湿器控制函数 ==================== */
void atomiz_turn_on(void)
{
    rt_pin_write(ATOMIZ_PIN, PIN_HIGH);
    lock_data();
    sensor_data.atomiz.current_status = RT_TRUE;
    unlock_data();
    send_actuator_update(ACTUATOR_ATOMIZ, sensor_data.atomiz.mode, RT_TRUE, 0);
}

void atomiz_turn_off(void)
{
    rt_pin_write(ATOMIZ_PIN, PIN_LOW);
    lock_data();
    sensor_data.atomiz.current_status = RT_FALSE;
    unlock_data();
    send_actuator_update(ACTUATOR_ATOMIZ, sensor_data.atomiz.mode, RT_FALSE, 0);
}


void buzzer_turn_on(void)
{
    /* 低电平触发，所以输出低电平时蜂鸣器响 */
    rt_pin_write(BUZZER_PIN, PIN_LOW);

    lock_data();
    sensor_data.buzzer.current_status = RT_TRUE;
    unlock_data();
    send_actuator_update(ACTUATOR_BUZZER, sensor_data.buzzer.mode, RT_TRUE, 0);
}

void buzzer_turn_off(void)
{
    /* 输出高电平，蜂鸣器停止 */
    rt_pin_write(BUZZER_PIN, PIN_HIGH);

    lock_data();
    sensor_data.buzzer.current_status = RT_FALSE;
    unlock_data();
    send_actuator_update(ACTUATOR_BUZZER, sensor_data.buzzer.mode, RT_FALSE, 0);
}

/* ==================== JQ8900 音频控制实现 ==================== */

/**
 * @brief 发送 JQ8900 两线串口指令
 * @param cmd     指令类型（如 0x07 指定曲目，0x04 停止等）
 * @param data    数据缓冲区（可为 NULL）
 * @param data_len 数据长度（无数据时为 0）
 */
rt_err_t audio_send_cmd(uint8_t cmd, uint8_t *data, uint8_t data_len)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized");
        return -RT_ERROR;
    }

    uint8_t buf[JQ_CMD_BUF_SIZE];
    uint8_t idx = 0;
    uint8_t sum = 0;

    buf[idx++] = JQ_CMD_START;   // 起始码 0xAA
    buf[idx++] = cmd;             // 指令类型
    buf[idx++] = data_len;        // 数据长度

    if (data != NULL && data_len > 0) {
        rt_memcpy(&buf[idx], data, data_len);
        idx += data_len;
    }

    // 计算校验和（起始码 + 指令类型 + 数据长度 + 数据）
    for (uint8_t i = 0; i < idx; i++) {
        sum += buf[i];
    }
    buf[idx++] = sum;             // 校验和

    // 发送指令
    rt_size_t sent = rt_device_write(audio_uart, 0, buf, idx);
    if (sent != idx) {
        LOG_E("JQ8900 send failed: sent=%d, expected=%d", sent, idx);
        return -RT_ERROR;
    }

    LOG_D("JQ8900 send cmd: %02X, len=%d", cmd, idx);
    return RT_EOK;
}

/**
 * @brief 设置播放模式（循环模式）
 * @param mode 模式：0x00 全盘循环，0x01 单曲循环，0x02 单曲停止...
 * @return RT_EOK 成功，否则失败
 */
rt_err_t audio_set_loop_mode(uint8_t mode)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized");
        return -RT_ERROR;
    }
    return audio_send_cmd(JQ_CMD_SET_MODE, &mode, 1);
}

/**
 * @brief 播放指定曲目（单曲循环模式）
 * @param track 曲目号（从 1 开始，对应根目录下 00001.mp3 等）
 */
void audio_play_single_loop(uint16_t track)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized, cannot play");
        return;
    }

    // 1. 设置为单曲循环模式
    if (audio_set_loop_mode(JQ_MODE_SINGLE_LOOP) != RT_EOK) {
        LOG_E("Failed to set single loop mode");
        return;
    }

    // 2. 发送指定曲目播放指令
    uint8_t data[2];
    data[0] = (uint8_t)((track >> 8) & 0xFF);   // 曲目高字节
    data[1] = (uint8_t)(track & 0xFF);          // 曲目低字节

    if (audio_send_cmd(JQ_CMD_PLAY_TRACK, data, 2) == RT_EOK) {
        lock_data();
        sensor_data.audio.current_status = RT_TRUE;
        unlock_data();
        send_actuator_update(ACTUATOR_AUDIO, sensor_data.audio.mode, RT_TRUE, track);
    }

    LOG_I("JQ8900 play track %d (loop mode)", track);
}

/**
 * @brief 停止当前播放
 */
void audio_stop(void)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized, cannot stop");
        return;
    }

    if (audio_send_cmd(JQ_CMD_STOP, NULL, 0) == RT_EOK) {
        lock_data();
        sensor_data.audio.current_status = RT_FALSE;
        unlock_data();
        send_actuator_update(ACTUATOR_AUDIO, sensor_data.audio.mode, RT_FALSE, 0);
    }

    LOG_I("JQ8900 stop playback");
}
/* ==================== 新增音频控制函数 ==================== */
void audio_pause(void)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized, cannot pause");
        return;
    }
    audio_send_cmd(JQ_CMD_PAUSE, NULL, 0);
    LOG_I("JQ8900 pause playback");
}

void audio_resume(void)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized, cannot resume");
        return;
    }
    audio_send_cmd(JQ_CMD_PLAY, NULL, 0);
    LOG_I("JQ8900 resume playback");
}

void audio_set_volume(uint8_t volume)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized, cannot set volume");
        return;
    }
    if (volume > 30) volume = 30;
    audio_send_cmd(JQ_CMD_SET_VOLUME, &volume, 1);
    LOG_I("JQ8900 set volume to %d", volume);
}

void audio_set_eq(uint8_t eq)
{
    if (audio_uart == RT_NULL) {
        LOG_E("Audio UART not initialized, cannot set EQ");
        return;
    }
    if (eq > 4) eq = 4;
    audio_send_cmd(JQ_CMD_SET_EQ, &eq, 1);
    LOG_I("JQ8900 set EQ to %d", eq);
}


/* ==================== 蜂鸣器辅助函数（阻塞式） ==================== */
/* 定时器回调函数：到期后发送事件给蜂鸣器线程 */
void buzzer_timer_callback(void *parameter)
{
    rt_event_send(system_events, EVENT_BUZZER_TIMEOUT);
}

/* 初始化蜂鸣器状态机 */
void buzzer_state_machine_init(void)
{
    buzzer_ctrl.state = BUZZER_STATE_IDLE;
    buzzer_ctrl.beep_duration = 0;
    buzzer_ctrl.interval_duration = 0;
    buzzer_ctrl.remaining_beeps = 0;
    /* 创建软件定时器（不启动） */
    buzzer_ctrl.timer = rt_timer_create("buzzer_timer",
                                   buzzer_timer_callback,
                                   RT_NULL,
                                   10,     // 初始10ms，实际会被动态修改
                                   RT_TIMER_FLAG_SOFT_TIMER | RT_TIMER_FLAG_ONE_SHOT);
    if (buzzer_ctrl.timer == RT_NULL) {
        LOG_E("Failed to create buzzer timer");
    }
}

/* 启动一个蜂鸣序列（非阻塞） */
void buzzer_start_alarm(uint16_t beep_ms, uint16_t interval_ms, uint8_t count)
{
    LOG_D("buzzer_start_alarm: beep=%dms, interval=%dms, count=%d", beep_ms, interval_ms, count);
    /* 停止当前正在进行的任何蜂鸣序列 */
    if (buzzer_ctrl.timer && buzzer_ctrl.timer->parent.flag & RT_TIMER_FLAG_ACTIVATED) {
        rt_timer_stop(buzzer_ctrl.timer);
        LOG_D("buzzer_start_alarm: stopped previous timer");
    }
    buzzer_turn_off();   // 立即关闭当前蜂鸣
    buzzer_ctrl.state = BUZZER_STATE_BEEP_ON;
    buzzer_ctrl.beep_duration = beep_ms;
    buzzer_ctrl.interval_duration = interval_ms;
    buzzer_ctrl.remaining_beeps = count;
    /* 立即开始蜂鸣 */
    buzzer_turn_on();
    /* 启动定时器，beep_ms 后触发停止 */
    rt_tick_t tick = rt_tick_from_millisecond(beep_ms);
    rt_timer_control(buzzer_ctrl.timer, RT_TIMER_CTRL_SET_TIME, &tick);
    rt_timer_start(buzzer_ctrl.timer);
    LOG_D("buzzer_start_alarm: timer started with %d ticks", tick);
}


/* ==================== MSH 命令导出 ==================== */
static void msh_audio_play(int argc, char **argv)
{
    if (argc != 2) {
        rt_kprintf("Usage: audio_play <track>\n");
        return;
    }
    uint16_t track = (uint16_t)atoi(argv[1]);
    track = 1;
    if (track == 0) {
        rt_kprintf("Track number must be >0\n");
        return;
    }
    audio_play_single_loop(track);
}
MSH_CMD_EXPORT(msh_audio_play, Play track in single loop);

static void msh_audio_stop(void)
{
    audio_stop();
}
MSH_CMD_EXPORT(msh_audio_stop, Stop playback);

static void msh_audio_pause(void)
{
    audio_pause();
}
MSH_CMD_EXPORT(msh_audio_pause, Pause playback);

static void msh_audio_resume(void)
{
    audio_resume();
}
MSH_CMD_EXPORT(msh_audio_resume, Resume playback);

static void msh_audio_volume(int argc, char **argv)
{
    if (argc != 2) {
        rt_kprintf("Usage: audio_volume <0-30>\n");
        return;
    }
    int vol = atoi(argv[1]);
    if (vol < 0) vol = 0;
    if (vol > 30) vol = 30;
    audio_set_volume((uint8_t)vol);
}
MSH_CMD_EXPORT(msh_audio_volume, Set volume (0-30));

static void msh_audio_eq(int argc, char **argv)
{
    if (argc != 2) {
        rt_kprintf("Usage: audio_eq <0-4>\n");
        rt_kprintf("0:Normal 1:Pop 2:Rock 3:Jazz 4:Classic\n");
        return;
    }
    int eq = atoi(argv[1]);
    if (eq < 0) eq = 0;
    if (eq > 4) eq = 4;
    audio_set_eq((uint8_t)eq);
}
MSH_CMD_EXPORT(msh_audio_eq, Set equalizer (0-4));
