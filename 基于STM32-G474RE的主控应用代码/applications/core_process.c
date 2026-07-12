/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-23     17625       the first version
 */
#include "ipc_manager.h"
#include "actuators.h"
#include "external_comm.h"
#include "core_process.h"
#include "common.h"
#include <rtdbg.h>
#include <board.h>
#include <math.h>
#include <string.h>


/* ==================== 线程管理函数 ==================== */
static rt_thread_t fusion_thread = RT_NULL;
static rt_bool_t fusion_running = RT_FALSE;
/* 定义时间管理线程控制块及标志 */
static rt_thread_t time_thread = RT_NULL;
static rt_bool_t time_running = RT_FALSE;

static rt_bool_t is_in_play_time(int hour, int minute);
static void fusion_thread_entry(void *parameter);

/* 时间管理线程入口 */
static void time_management_thread_entry(void *parameter)
{
    LOG_I("Time management thread started");

    while (time_running)
    {
        RTC_TimeTypeDef rtc_time;
        RTC_DateTypeDef rtc_date;

        /* 读取 RTC 时间 */
        if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) == HAL_OK)
        {
            HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN); // 解锁

            /* 构建时间消息并发送到显示队列 */
            sensor_msg_t msg;
            msg.type = SENSOR_TIME;
            msg.timestamp = rt_tick_get();
            msg.data.time.hours = rtc_time.Hours;
            msg.data.time.minutes = rtc_time.Minutes;
            msg.data.time.seconds = rtc_time.Seconds;
            msg.data.time.day = rtc_date.Date;
            msg.data.time.month = rtc_date.Month;
            msg.data.time.year = 2000 + rtc_date.Year;   // 将偏移量转换为完整年份;

            send_to_mq(display_queue, &msg);

            if (is_in_play_time(msg.data.time.hours,msg.data.time.minutes)!= RT_FALSE)
            {
                if (sensor_data.audio.current_status == RT_FALSE) {
                    command_t cmd;
                    rt_memset(&cmd, 0, sizeof(cmd));
                    cmd.type = MSG_AUDIO;
                    cmd.timestamp = rt_tick_get();
                    cmd.source = INTER;   /* 这里作为内部决策 */
                    cmd.data.audio.mode = AUDIO_AUTO;   /* 保持自动模式 */
                    cmd.data.audio.audio_status = RT_TRUE;
                    send_cmd_to_mq(audio_queue,&cmd);
                }
            }
            else
            {
                if (sensor_data.audio.current_status) {
                    command_t cmd;
                    rt_memset(&cmd, 0, sizeof(cmd));
                    cmd.type = MSG_AUDIO;
                    cmd.timestamp = rt_tick_get();
                    cmd.source = INTER;   /* 这里作为内部决策 */
                    cmd.data.audio.mode = AUDIO_AUTO;   /* 保持自动模式 */
                    cmd.data.audio.audio_status = RT_FALSE;
                    send_cmd_to_mq(audio_queue,&cmd);
                }
            }
        }
        /* 每秒/分钟更新一次 */
        rt_thread_mdelay(THREAD_TIME_TICK);
    }

    LOG_I("Time management thread stopped");
}


/* ==================== 线程管理函数 ==================== */
int process_threads_init(void)
{
    LOG_I("core process threads...");

    time_running = RT_TRUE;
    fusion_running = RT_TRUE;

    time_thread = rt_thread_create("time_mgmt",
        time_management_thread_entry,
        RT_NULL,
        THREAD_TIME_STACK_SIZE,          // 栈大小
        THREAD_TIME_PRIORITY,             // 优先级
        10);
    if (time_thread == RT_NULL)
    {
        LOG_E("Failed to create time management thread");
        return -RT_ERROR;
    }
    rt_thread_startup(time_thread);

    fusion_thread = rt_thread_create("fusion",
          fusion_thread_entry,
          RT_NULL,
          THREAD_FUSION_STACK_SIZE,
          THREAD_FUSION_PRIORITY,
          10);
    if (fusion_thread == RT_NULL)
    {
        LOG_E("Failed to create fusion management thread");
        return -RT_ERROR;
    }
    rt_thread_startup(fusion_thread);


    LOG_I("Sensor threads started successfully");
    return RT_EOK;

}

void process_threads_stop(void)
{
    LOG_I("core process threads...");

    time_running = RT_FALSE;
    fusion_running = RT_FALSE;

    rt_thread_mdelay(100);

    if (time_thread != RT_NULL)
    {
        rt_thread_delete(time_thread);
        time_thread = RT_NULL;
    }

    if (fusion_thread != RT_NULL)
    {
        rt_thread_delete(fusion_thread);
        fusion_thread = RT_NULL;
    }
    LOG_I("Time management thread stopped");
}



/* 判断当前时间是否在播放时间段内 */
static rt_bool_t is_in_play_time(int hour, int minute)
{
    /* 上午时段 8:30 - 11:30 */
    if (hour > 8 && hour < 11) {
        return RT_TRUE;
    } else if (hour == 8) {
        if (minute >= 30) return RT_TRUE;
    } else if (hour == 11) {
        if (minute < 30) return RT_TRUE;
    }

    /* 下午时段 14:00 - 17:00 */
    if (hour > 14 && hour < 17) {
        return RT_TRUE;
    } else if (hour == 14) {
        return RT_TRUE;           // 14:00 开始
    } else if (hour == 17) {
        return RT_FALSE;          // 17:00 不包含
    }

    return RT_FALSE;
}



/* 根据种类获取目标参数 */
const target_env_t* get_target_by_mushroom_type(mushroom_type_t type)
{
    switch (type) {
    case MUSHROOM_TYPE_HONGGU:      return &target_env_honggu;
    case MUSHROOM_TYPE_LANGU:       return &target_env_langu;
    case MUSHROOM_TYPE_LVGU:        return &target_env_lvgu;
    case MUSHROOM_TYPE_YUNGU:       return &target_env_yungu;
    case MUSHROOM_TYPE_HUANGGU:     return &target_env_huanggu;
    default:                        return &target_env_default;
    }
}

/* ==================== 温度加权融合（80% 棚内 + 20% 天气温度） ==================== */
/* 只调整温度目标范围，湿度和光照不做融合 */
static void apply_temperature_weighted_fusion(target_env_t *target,
                                               float indoor_temp, float weather_temp)
{
    const float INDOOR_W = 0.8f;
    const float WEATHER_W = 0.2f;

    float fused_temp = INDOOR_W * indoor_temp + WEATHER_W * weather_temp;

    if (fused_temp > target->temp_max) {
        target->temp_max += (fused_temp - target->temp_max) * 0.2f;
    } else if (fused_temp < target->temp_min) {
        target->temp_min -= (target->temp_min - fused_temp) * 0.2f;
    }

    /* 边界保护 */
    if (target->temp_min < 0) target->temp_min = 0;
    if (target->temp_max > 50) target->temp_max = 50;
}

/* ==================== 模糊控制器（Mamdani型） ==================== */
static float trimf(float x, float a, float b, float c)
{
    if (x <= a || x >= c) return 0.0f;
    if (x <= b) return (x - a) / (b - a);
    return (c - x) / (c - b);
}

#define NB  0
#define NS  1
#define ZO  2
#define PS  3
#define PB  4

static const int fuzzy_rules[5][5] = {

  { NB,   NB,   NB,   NS,   NS  },        /* e=NB (极冷) */
  { NB,   NS,   NS,   ZO,   ZO  },        /* e=NS (稍冷) */
  { NS,   NS,   ZO,   PS,   PB  },        /* e=ZO (正常) */
  { ZO,   ZO,   PS,   PB,   PB  },        /* e=PS (稍热) */
  { PB,   PB,   PB,   PB,   PB  }        /* e=PB (极热) */
};

static const struct {
    float a, b, c;
} out_mf[5] = {
    {  0,  0, 10 },
    { 20, 35, 50 },
    {  5, 15, 25 },
    { 40, 65, 90 },
    { 70, 90, 100 }
};

static float fuzzy_control(float e, float ec, float e_max, float ec_max)
{
    float e_norm = e / e_max;
    float ec_norm = ec / ec_max;
    if (e_norm > 1.0f) e_norm = 1.0f;
    if (e_norm < -1.0f) e_norm = -1.0f;
    if (ec_norm > 1.0f) ec_norm = 1.0f;
    if (ec_norm < -1.0f) ec_norm = -1.0f;

    float e_mf[5] = {0};
    e_mf[NB] = trimf(e_norm, -1.2f, -1.0f, -0.5f);
    e_mf[NS] = trimf(e_norm, -1.0f, -0.5f, 0.0f);
    e_mf[ZO] = trimf(e_norm, -0.5f, 0.0f, 0.5f);
    e_mf[PS] = trimf(e_norm, 0.0f, 0.5f, 1.0f);
    e_mf[PB] = trimf(e_norm, 0.5f, 1.0f, 1.2f);

    float ec_mf[5] = {0};
    ec_mf[NB] = trimf(ec_norm, -1.2f, -1.0f, -0.5f);
    ec_mf[NS] = trimf(ec_norm, -1.0f, -0.5f, 0.0f);
    ec_mf[ZO] = trimf(ec_norm, -0.5f, 0.0f, 0.5f);
    ec_mf[PS] = trimf(ec_norm, 0.0f, 0.5f, 1.0f);
    ec_mf[PB] = trimf(ec_norm, 0.5f, 1.0f, 1.2f);

    float rule_activation[5][5] = {0};
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            rule_activation[i][j] = fminf(e_mf[i], ec_mf[j]);
        }
    }

    float numerator = 0.0f, denominator = 0.0f;
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            int out_idx = fuzzy_rules[i][j];
            float mu = rule_activation[i][j];
            float center = (out_mf[out_idx].a + out_mf[out_idx].b + out_mf[out_idx].c) / 3.0f;
            numerator += mu * center;
            denominator += mu;
        }
    }
    if (denominator < 1e-6f) return 0.0f;
    float u = numerator / denominator;
    if (u < 0) u = 0;
    if (u > 100) u = 100;
    return u;
}

/* ==================== 多因子协同调控（模糊控制版本） ==================== */
static void generate_control_commands(const target_env_t *target,
                                       float current_temp, float current_humi,
                                       float current_light, int current_eco2,
                                       float current_soil_temp, int current_soil_humi,
                                       float prev_temp, float prev_humi, float prev_light,int prev_eco2)
{
    command_t cmd;

    /* 静态变量：记录上一次发送的命令值（初始值表示从未发送） */
    static int last_fan_speed = -1;
    static rt_bool_t last_fan_status = RT_FALSE;
    static rt_bool_t last_atomiz_status = RT_FALSE;
    static int last_led_brightness = -1;
    static rt_bool_t last_led_status = RT_FALSE;
    static rt_bool_t last_pump_status = RT_FALSE;
    static float prev_temp_error = 0.0f;   // 记录上一次温度误差，用于计算变化率

    rt_bool_t fan_status_new;
    int fan_speed_new;

    /* ---------- 1. 风扇控制 ---------- */
    // 计算温度误差 e = 当前温度 - 目标温度中心值
    float temp_center = (target->temp_min + target->temp_max) * 0.5f;
    float e_temp = current_temp - temp_center;

    // 首次运行时避免 ec 跳变（静态变量持久化）
    static rt_bool_t fuzzy_first_call = RT_TRUE;
    if (fuzzy_first_call) {
        prev_temp_error = e_temp;          // 将第一次误差记录下来
        fuzzy_first_call = RT_FALSE;
    }

    // 计算误差变化率 ec = 当前误差 - 上一次误差
    float ec_temp = e_temp - prev_temp_error;
    prev_temp_error = e_temp;   // 更新静态变量

    // 动态计算模糊控制输入尺度
    float temp_range_half = (target->temp_max - target->temp_min) * 0.5f;
    float e_max_temp = temp_range_half * 2.0f;   // 可调系数，建议 2~4
    if (e_max_temp < 0.5f) e_max_temp = 0.5f;
    float ec_max_temp = e_max_temp * 0.3f;
    if (ec_max_temp < 0.1f) ec_max_temp = 0.1f;

    // 调用模糊控制器，输出 u 在 0~100
    float u_fan = fuzzy_control(e_temp, ec_temp, e_max_temp, ec_max_temp);

    // 将 u 映射到风扇转速（0~100），并设置死区
    fan_speed_new = (int)u_fan;
    if (fan_speed_new < 30) fan_speed_new = 0;    // 死区：低于30%则关闭
    if (fan_speed_new > 100) fan_speed_new = 100;

    fan_status_new = (fan_speed_new > 0) ? RT_TRUE : RT_FALSE;

    if (fan_status_new != last_fan_status || abs(fan_speed_new - last_fan_speed) > DEADZONE) {
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.type = MSG_FAN;
        cmd.source = INTER;
        cmd.data.fan.mode = FAN_AUTO;
        cmd.data.fan.fan_status = fan_status_new;
        cmd.data.fan.speed = fan_speed_new;
        send_cmd_to_mq(fan_queue, &cmd);
        last_fan_status = fan_status_new;
        last_fan_speed = fan_speed_new;
        LOG_D("Fan command sent: status=%d, speed=%d", fan_status_new, fan_speed_new);
    }

    /* ---------- 2. 加湿器控制（开关型） ---------- */
    rt_bool_t atomiz_status_new  = last_atomiz_status;
    if (current_humi  < target->humid_min) {
        atomiz_status_new  = RT_TRUE;
    } else if (current_humi > target->humid_max - ALARM_HUMI_OFFSET) {
        atomiz_status_new = RT_FALSE;
    }

    if (atomiz_status_new != last_atomiz_status) {
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.type = MSG_ATOMIZ;
        cmd.source = INTER;
        cmd.data.atomiz.mode = ATOMIZ_AUTO;
        cmd.data.atomiz.atomiz_status = atomiz_status_new;
        send_cmd_to_mq(atomiz_queue, &cmd);
        last_atomiz_status = atomiz_status_new;
        LOG_D("Atomizer command sent: status=%d", atomiz_status_new);
    }

    /* ---------- 3. LED 补光（连续调光） ---------- */
    /* LED 控制：线性映射补光 */
    int led_brightness_new = 0;
    rt_bool_t led_status_new = RT_FALSE;

    if (current_light < target->light_min - ALARM_LIGHT_OFFSET) {
        /* 红区：严重缺光，100% 亮度 */
        led_brightness_new = 100;
    } else if (current_light < target->light_min + ALARM_LIGHT_OFFSET) {
        /* 黄区：从 0% 到 100% 线性映射 */
        float low = target->light_min - ALARM_LIGHT_OFFSET;
        float high = target->light_min + ALARM_LIGHT_OFFSET;
        float ratio = (high - current_light) / (high - low);  // 0~1
        led_brightness_new = (int)(ratio * 100);
    } else {
        /* 光照充足，关闭 */
        led_brightness_new = 0;
    }

    if (led_brightness_new != 0) {
        led_status_new = RT_TRUE;
    }else {
        led_status_new = RT_FALSE;
    }

    if (led_status_new != last_led_status || abs(led_brightness_new - last_led_brightness) > DEADZONE) {
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.type = MSG_LED;
        cmd.source = INTER;
        cmd.data.led.mode = LED_AUTO;
        cmd.data.led.led_status = led_status_new;
        cmd.data.led.brightness = led_brightness_new;
        send_cmd_to_mq(led_queue, &cmd);
        last_led_status = led_status_new;
        last_led_brightness = led_brightness_new;
        LOG_D("LED command sent: status=%d, brightness=%d", led_status_new, led_brightness_new);
    }

    /* ---------- 4. 水泵控制（简单阈值） ---------- */
    rt_bool_t pump_status_new = last_pump_status;
    if (current_soil_humi < target->soil_humid_min) {
        pump_status_new = RT_TRUE;
    } else if (current_soil_humi > target->soil_humid_max - ALARM_SOIL_HUMI_OFFSET) {
        pump_status_new = RT_FALSE;
    }

    if (pump_status_new != last_pump_status) {
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.type = MSG_PUMP;
        cmd.source = INTER;
        cmd.data.pump.mode = PUMP_AUTO;
        cmd.data.pump.pump_status = pump_status_new;
        send_cmd_to_mq(pump_queue, &cmd);
        last_pump_status = pump_status_new;
        LOG_D("Pump command sent: status=%d", pump_status_new);
    }

}

/* 报警判断函数（封装所有报警逻辑） */
static uint32_t check_and_trigger_alarms(
    float current_temp, float current_humi,
    int current_soil_humi, float current_soil_temp,
    int current_eco2, float current_light,
    const target_env_t *target)
{
    /* 静态变量：记录上一次报警状态 */
    static rt_bool_t last_any_alarm = RT_FALSE;
    rt_bool_t any_alarm = RT_FALSE;

    /* 空气温度过高/过低 */
    if (current_temp > target->temp_max + ALARM_TEMP_OFFSET) any_alarm = RT_TRUE;
    if (current_temp < target->temp_min - ALARM_TEMP_OFFSET) any_alarm = RT_TRUE;

    /* 空气湿度过高/过低 */
    if (current_humi > target->humid_max + ALARM_HUMI_OFFSET) any_alarm = RT_TRUE;
    if (current_humi < target->humid_min - ALARM_HUMI_OFFSET) any_alarm = RT_TRUE;

    /* 土壤湿度干旱/过湿 */
    if (current_soil_humi < target->soil_humid_min - ALARM_SOIL_HUMI_OFFSET) any_alarm = RT_TRUE;
    if (current_soil_humi > target->soil_humid_max + ALARM_SOIL_HUMI_OFFSET) any_alarm = RT_TRUE;

    /* 土壤温度过高/过低 */
    if (current_soil_temp > target->soil_temp_max + ALARM_SOIL_TEMP_OFFSET) any_alarm = RT_TRUE;
    if (current_soil_temp < target->soil_temp_min - ALARM_SOIL_TEMP_OFFSET) any_alarm = RT_TRUE;

    /* 土壤温度过高/过低 */
    if (current_light > target->light_max + ALARM_LIGHT_OFFSET) any_alarm = RT_TRUE;
    if (current_light < target->light_min - ALARM_LIGHT_OFFSET) any_alarm = RT_TRUE;

    /* CO₂浓度过高  */
    if (current_eco2 > target->eco2_max + ALARM_CO2_OFFSET) any_alarm = RT_TRUE;

    /* ---- 状态变化检测 ---- */
    uint32_t events_to_send = 0;
    if (any_alarm && !last_any_alarm) {
        events_to_send |= EVENT_ALARM_GENERAL;   // 从无到有，触发蜂鸣
    } else if (!any_alarm && last_any_alarm) {
        events_to_send |= EVENT_ALARM_ALL_CLEAR; // 从有到无，停止蜂鸣
    }
    last_any_alarm = any_alarm;

    return events_to_send;  // 返回要发送的事件（可能为0）
}


/* ==================== 融合线程入口 ==================== */
static void fusion_thread_entry(void *parameter)
{
    sensor_msg_t msg;
    rt_tick_t last_calc_time = 0;
    const rt_tick_t calc_interval = rt_tick_from_millisecond(2000);

    sensor_msg_t latest_soil = {0};
    sensor_msg_t latest_soil_temp = {0};
    sensor_msg_t latest_temp = {0};
    sensor_msg_t latest_humi = {0};
    sensor_msg_t latest_light = {0};
    sensor_msg_t latest_eco2 = {0};
    sensor_msg_t latest_vision = {0};
    float current_weather_temp = 0.0f;  // 默认值

    float prev_temp = 25.0f, prev_humi = 60.0f, prev_light = 500.0f;
    int prev_eco2 = 400;
    static rt_bool_t first_run = RT_TRUE;   // 新增：首次运行标志

    //LOG_I("Fusion thread started (with temperature weighted fusion & fuzzy control)");

    while (fusion_running)
    {
        rt_ssize_t result = rt_mq_recv(fusion_queue, &msg, sizeof(sensor_msg_t), RT_WAITING_FOREVER);
        if (result != sizeof(sensor_msg_t)) continue;
        //LOG_D("[FUSION] Received msg type=%d", msg.type);
        switch (msg.type) {
        case SENSOR_SOIL_MOISTURE:   latest_soil = msg; break;
        case SENSOR_SOIL_TEMPERATURE: latest_soil_temp = msg; break;
        case SENSOR_TEMPERATURE:     latest_temp = msg; break;
        case SENSOR_HUMIDITY:        latest_humi = msg; break;
        case SENSOR_LIGHT:           latest_light = msg; break;
        case SENSOR_ECO2:            latest_eco2 = msg; break;
        case SENSOR_AI:              latest_vision = msg; break;
        default: continue;
        }

        rt_tick_t now = rt_tick_get();
        rt_tick_t timeout = rt_tick_from_millisecond(8000);

        rt_bool_t data_fresh = RT_TRUE;
        if (latest_soil.timestamp + timeout < now) data_fresh = RT_FALSE;
        if (latest_temp.timestamp + timeout < now) data_fresh = RT_FALSE;
        if (latest_humi.timestamp + timeout < now) data_fresh = RT_FALSE;
        if (latest_light.timestamp + timeout < now) data_fresh = RT_FALSE;

        if (now - last_calc_time < calc_interval) {
            continue;
        }

        if (!data_fresh){
            LOG_W("Data not fresh, skip calculation");
            continue;
        }

        float current_temp = (latest_temp.timestamp + timeout > now) ? latest_temp.data.env.temperature : 25.0f;
        float current_humi = (latest_humi.timestamp + timeout > now) ? latest_humi.data.env.humidity : 60.0f;
        float current_light = (latest_light.timestamp + timeout > now) ? latest_light.data.light_lux : 500.0f;
        int current_eco2 = (latest_eco2.timestamp + timeout > now) ? (int)latest_eco2.data.air.eco2 : 400;
        int current_soil_humi = (latest_soil.timestamp + timeout > now) ? (int)latest_soil.data.soil.humidity_percent : 50;
        float current_soil_temp = (latest_soil_temp.timestamp + timeout > now) ? latest_soil_temp.data.soil_temperature : 22.0f;

        if (first_run) {
            prev_temp = current_temp;
            prev_humi = current_humi;
            prev_light = current_light;
            prev_eco2 = current_eco2;
            first_run = RT_FALSE;
            LOG_I("Fusion first run: prev_light initialized to %.1f lux", prev_light);
        }

        static mushroom_type_t current_type = MUSHROOM_TYPE_UNKNOWN;
        if (latest_vision.timestamp + timeout > now && latest_vision.type == SENSOR_AI) {
            current_type = (mushroom_type_t)latest_vision.data.ai.type;
        }

        target_env_t target = *get_target_by_mushroom_type(current_type);

        // 调用报警判断函数，获取需要发送的事件掩码
        uint32_t events = check_and_trigger_alarms(
            current_temp, current_humi,
            current_soil_humi, current_soil_temp,
            current_eco2, current_light,
            &target);

        // 发送所有事件
        if (events) {
            rt_uint32_t clear_mask = EVENT_ALARM_GENERAL | EVENT_ALARM_ALL_CLEAR;
            rt_event_recv(system_events, clear_mask,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          0, NULL);   // 非阻塞立即清除
            post_event(events);
        }

        if (g_weather_mutex != RT_NULL) {
            rt_mutex_take(g_weather_mutex, RT_WAITING_FOREVER);
            weather_day_t *today = &g_weather_data.days[0];
            current_weather_temp = (today->high + today->low) * 0.5f;
            rt_mutex_release(g_weather_mutex);
        }

        /* 温度加权融合（如果有天气数据） */
        if (current_weather_temp != 0) {
            apply_temperature_weighted_fusion(&target, current_temp, current_weather_temp);
        }

        generate_control_commands(&target,
                                  current_temp, current_humi, current_light, current_eco2,
                                  current_soil_temp, current_soil_humi,
                                  prev_temp, prev_humi, prev_light,prev_eco2);

        prev_temp = current_temp;
        prev_humi = current_humi;
        prev_light = current_light;
        prev_eco2 = current_eco2;

        last_calc_time = now;
    }

}















