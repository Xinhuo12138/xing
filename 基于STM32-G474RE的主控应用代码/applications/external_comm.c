/*
#include <external_comm.h>
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-31     17625       the first version
 */

#include "common.h"
#include <rtdbg.h>
#include "sensors.h"
#include "actuators.h"
#include "external_comm.h"
#include "ipc_manager.h"
#include "core_process.h"
#include <math.h>
#include <time.h>

/* 全局天气数据定义 */
weather_3day_t g_weather_data;
rt_mutex_t g_weather_mutex = RT_NULL;


time_t ntp_sync_to_rtc(const char *host);

static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size);
static void handle_onenet_command(uint8_t *recv_data, size_t recv_size);
/* 将 value 四舍五入到 n 位小数 */
static double round_to_n(double value, int n)
{
    double scale = pow(10.0, n);
    return round(value * scale) / scale;
}

/* ==================== 本地显示线程控制 ==================== */
rt_bool_t comm_running  = RT_FALSE;
rt_thread_t comm_thread  = RT_NULL;

/* ==================== 上传线程控制 ==================== */
rt_bool_t upload_running  = RT_FALSE;
rt_thread_t upload_thread  = RT_NULL;

/* ==================== 天气订阅线程控制 ==================== */
rt_bool_t weather_running = RT_FALSE;
rt_thread_t weather_thread  = RT_NULL;

/* ==================== NTP线程控制 ==================== */
rt_bool_t ntp_running  = RT_FALSE;
rt_thread_t ntp_thread  = RT_NULL;

/* ==================== 网络工作线程控制 ==================== */
rt_mq_t net_req_queue = RT_NULL;
rt_bool_t net_worker_running = RT_FALSE;
rt_thread_t net_worker_thread = RT_NULL;

/* ==================== 屏幕命令解析函数 ==================== */
/**
 * @brief 解析TJC串口屏命令帧
 * @param frame 接收到的命令帧数据
 * @param cmd 解析后的命令结构体
 * @return RT_TRUE: 解析成功, RT_FALSE: 解析失败
 *
 * 命令帧格式: 88 77 88 | 00 | 01 | 01 | 00 | FF FF FF
 *                          帧头  |来源|类型|模式|数值|     帧尾
 */
rt_bool_t parse_tjc_command(uint8_t *frame, command_t *cmd)
{
    /* 验证帧头 */
    if (frame[0] != FRAME_HEADER_1 ||
        frame[1] != FRAME_HEADER_2 ||
        frame[2] != FRAME_HEADER_3) {
        return RT_FALSE;
    }

    /* 验证帧尾 */
    if (frame[7] != FRAME_TAIL_1 ||
        frame[8] != FRAME_TAIL_2 ||
        frame[9] != FRAME_TAIL_3) {
        return RT_FALSE;
    }

    /* 提取命令信息 */
    uint8_t source = frame[3];      // 命令来源
    uint8_t type = frame[4];        // 消息类型
    uint8_t mode = frame[5];        // 运行模式
    uint8_t value = frame[6];       // 数值

    /* 填充命令结构体 */
    cmd->timestamp = rt_tick_get();

    /* 设置命令来源 */
    cmd->source = (source == CMD_SOURCE_CLOUD) ? CLOUD : LOCAL;

    /* 根据消息类型填充命令数据 */
    switch (type) {
        case CMD_TYPE_FAN:
            cmd->type = MSG_FAN;
            cmd->data.fan.mode = (mode == MODE_AUTO) ? FAN_AUTO : FAN_MANU;
            if (mode == MODE_MANUAL) {
                cmd->data.fan.fan_status = (value != 0);
                cmd->data.fan.speed = value;  // 风扇速度(0-100)
            }
            break;

        case CMD_TYPE_PUMP:
            cmd->type = MSG_PUMP;
            cmd->data.pump.mode = (mode == MODE_AUTO) ? PUMP_AUTO : PUMP_MANU;
            if (mode == MODE_MANUAL) {
                cmd->data.pump.pump_status = (value == STATE_ON);
            }
            break;

        case CMD_TYPE_LED:
            cmd->type = MSG_LED;
            cmd->data.led.mode = (mode == MODE_AUTO) ? LED_AUTO : LED_MANU;
            if (mode == MODE_MANUAL) {
                cmd->data.led.led_status = (value != 0);
                cmd->data.led.brightness = value;  // LED亮度(0-100)
            }
            break;

        case CMD_TYPE_BUZZER:
            cmd->type = MSG_BUZZER;
            cmd->data.buzzer.mode = (mode == MODE_AUTO) ? BUZZER_AUTO : BUZZER_MANU;
            if (mode == MODE_MANUAL) {
                cmd->data.buzzer.buzzer_status = (value == STATE_ON);
            }
            break;

        case CMD_TYPE_ATOMIZ:
            cmd->type = MSG_ATOMIZ;
            cmd->data.atomiz.mode = (mode == MODE_AUTO) ? ATOMIZ_AUTO : ATOMIZ_MANU;
            if (mode == MODE_MANUAL) {
                cmd->data.atomiz.atomiz_status = (value == STATE_ON);
            }
            break;

        case CMD_TYPE_AUDIO:
            cmd->type = MSG_AUDIO;
            cmd->data.audio.mode = (mode == MODE_AUTO) ? AUDIO_AUTO : AUDIO_MANU;
            if (mode == MODE_MANUAL) {
                /* value = 1 表示开始，0 表示停止 */
                cmd->data.audio.audio_status = (value == STATE_ON);
            }
            break;

        default:
            LOG_W("Unknown command type: 0x%02X", type);
            return RT_FALSE;
    }

    LOG_D("Parsed TJC command: type=%d, mode=%d, value=%d",
          type, mode, value);
    return RT_TRUE;
}

/**
 * @brief 处理接收到的串口命令
 */
void process_received_commands(void)
{
    static uint8_t rx_buffer[FRAME_LENGTH];
    static uint8_t frame_index = 0;
    static rt_bool_t in_frame = RT_FALSE;

    while (getRingBufferLength() > 0) {
        uint8_t byte = read1ByteFromRingBuffer(0);
        deleteRingBuffer(1);  // 删除已读取的字节

        /* 帧头检测 */
        if (!in_frame) {
            if (byte == FRAME_HEADER_1) {
                rx_buffer[0] = byte;
                frame_index = 1;
                in_frame = RT_TRUE;
            }
            continue;
        }

        /* 收集帧数据 */
        if (frame_index < FRAME_LENGTH) {
            rx_buffer[frame_index++] = byte;

            /* 检查是否收集完整帧 */
            if (frame_index == FRAME_LENGTH) {
                /* 验证帧尾 */
                if (rx_buffer[FRAME_LENGTH-3] == FRAME_TAIL_1 &&
                    rx_buffer[FRAME_LENGTH-2] == FRAME_TAIL_2 &&
                    rx_buffer[FRAME_LENGTH-1] == FRAME_TAIL_3) {

                    /* 解析命令 */
                    command_t cmd;
                    if (parse_tjc_command(rx_buffer, &cmd)) {
                        /* 根据命令类型发送到对应的邮箱 */
                        switch (cmd.type) {
                            case MSG_FAN:
                                send_command(fan_mailbox, &cmd);
                                break;
                            case MSG_PUMP:
                                send_command(pump_mailbox, &cmd);
                                break;
                            case MSG_LED:
                                send_command(led_mailbox, &cmd);
                                break;
                            case MSG_BUZZER:
                                send_command(buzzer_mailbox, &cmd);
                                break;
                            case MSG_ATOMIZ:
                                send_command(atomiz_mailbox, &cmd);
                                break;
                            case MSG_AUDIO:
                                send_command(audio_mailbox, &cmd);
                                break;
                            default:
                                break;
                        }

                        LOG_I("TJC command processed: type=%d", cmd.type);
                    }
                }

                /* 重置帧状态 */
                in_frame = RT_FALSE;
                frame_index = 0;
            }
        } else {
            /* 帧长度错误，重置 */
            in_frame = RT_FALSE;
            frame_index = 0;
        }
    }
}

/* ==================== 本地显示函数  ==================== */
void process_display_thread(void)
{
    /* 从显示队列读取传感器消息 */
    sensor_msg_t sensor_msg;
    rt_size_t received_bytes = 0;

    rt_ssize_t result = rt_mq_recv(display_queue, &sensor_msg,
                MSG_QUEUE_ITEM_SIZE, RT_WAITING_FOREVER);

    const target_env_t *target = get_target_by_mushroom_type(sensor_data.vision.type);

    if (result > 0)
    {
        //LOG_D("[DISPLAY] Received type=%d", sensor_msg.type);
        received_bytes = (rt_size_t)result;
        // 验证接收到的数据长度
        if (received_bytes == sizeof(sensor_msg_t)){
            /* 根据传感器类型更新TJC屏幕 */
            switch (sensor_msg.type)
            {
                case SENSOR_SOIL_MOISTURE:{
                    // 显示土壤湿度
                    char soil_humid_str[16];
                    rt_snprintf(soil_humid_str, sizeof(soil_humid_str), "%.1f%%", sensor_msg.data.soil.humidity_percent);
                    tjc_send_txt("sensor.soil_humi", "txt", soil_humid_str);
                    float val = sensor_msg.data.soil.humidity_percent;
                    float offset = ALARM_SOIL_HUMI_OFFSET;
                    int state;
                    if (val >= target->soil_humid_min && val <= target->soil_humid_max)
                        state = PIC_GREEN;
                    else if (val < target->soil_humid_min - offset||
                             val > target->soil_humid_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.soil_humi_p", "pic", state);
                    break;
                }
                case SENSOR_SOIL_TEMPERATURE:{
                    // 显示土壤温度
                    char soil_temp_str[16];
                    rt_snprintf(soil_temp_str, sizeof(soil_temp_str), "%.1fC", sensor_msg.data.soil_temperature);
                    tjc_send_txt("sensor.soil_temp", "txt", soil_temp_str);
                    float val = sensor_msg.data.soil_temperature;
                    float offset = ALARM_SOIL_TEMP_OFFSET;
                    int state;
                    if (val >= target->soil_temp_min && val <= target->soil_temp_max)
                        state = PIC_GREEN;
                    else if (val < target->soil_temp_min - offset||
                             val > target->soil_temp_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.soil_temp_p", "pic", state);
                    break;
                }
                case SENSOR_TEMPERATURE:{
                    // 显示温度
                    char temp_str[16];
                    rt_snprintf(temp_str, sizeof(temp_str), "%.1fC", sensor_msg.data.env.temperature);
                    tjc_send_txt("sensor.temp", "txt", temp_str);
                    float val = sensor_msg.data.env.temperature;
                    float offset = ALARM_TEMP_OFFSET;
                    int state;
                    if (val >= target->temp_min && val <= target->temp_max)
                        state = PIC_GREEN;
                    else if (val < target->temp_min - offset||
                             val > target->temp_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.temp_p", "pic", state);

                    break;
                }
                case SENSOR_HUMIDITY:{
                    // 显示湿度
                    char humid_str[16];
                    rt_snprintf(humid_str, sizeof(humid_str), "%.1f%%", sensor_msg.data.env.humidity);
                    tjc_send_txt("sensor.humi", "txt", humid_str);
                    float val = sensor_msg.data.env.humidity;
                    float offset = ALARM_HUMI_OFFSET;
                    int state;
                    if (val >= target->humid_min && val <= target->humid_max)
                        state = PIC_GREEN;
                    else if (val < target->humid_min - offset||
                             val > target->humid_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.humi_p", "pic", state);
                    break;
                }
                case SENSOR_LIGHT:{
                    // 显示光照
                    char light_str[16];
                    rt_snprintf(light_str, sizeof(light_str), "%.0f lux", sensor_msg.data.light_lux);
                    tjc_send_txt("sensor.light_level", "txt", light_str);
                    float val = sensor_msg.data.light_lux;
                    float offset = ALARM_LIGHT_OFFSET;
                    int state;
                    if (val >= target->light_min && val <= target->light_max)
                        state = PIC_GREEN;
                    else if (val < target->light_min - offset||
                             val > target->light_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.light_level_p", "pic", state);
                    break;
                }
                case SENSOR_ECO2:{
                    // 显示CO2
                    char co2_str[16];
                    rt_snprintf(co2_str, sizeof(co2_str), "%d ppm", sensor_msg.data.air.eco2);
                    tjc_send_txt("sensor.eco2", "txt", co2_str);
                    float val = sensor_msg.data.air.eco2;
                    float offset = ALARM_CO2_OFFSET;
                    int state;
                    if (val >= target->eco2_min && val <= target->eco2_max)
                        state = PIC_GREEN;
                    else if (val < target->eco2_min - offset||
                             val > target->eco2_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.eco2_p", "pic", state);
                    break;
                }
                case SENSOR_TVOC:{
                    // 显示TVOC
                    char tvoc_str[16];
                    rt_snprintf(tvoc_str, sizeof(tvoc_str), "%d ppb", sensor_msg.data.air.tvoc);
                    tjc_send_txt("sensor.tvoc", "txt", tvoc_str);
                    float val = sensor_msg.data.air.tvoc;
                    float offset = ALARM_TVOC_OFFSET;
                    int state;
                    if (val >= target->tvoc_min && val <= target->tvoc_max)
                        state = PIC_GREEN;
                    else if (val < target->tvoc_min - offset||
                             val > target->tvoc_max + offset)
                        state = PIC_RED;
                    else
                        state = PIC_YELLOW;
                    tjc_send_val("sensor.tvoc_p", "pic", state);
                    break;
                }

                case ACTUATOR_FAN:   // 风扇
                {
                    // 显示开关状态和转速百分比（如果 value 有效）
                    const char *state_str = sensor_msg.data.actor.state ? "已启动" : "已停止";
                    tjc_show_chinese("actuator.fan", state_str);
                    tjc_send_val("actuator.fan_statu", "val", sensor_msg.data.actor.state);
                    tjc_send_val("actuator.fan_mode", "val", sensor_msg.data.actor.work_mode);
                    tjc_send_val("actuator.fan_speed", "val", sensor_msg.data.actor.value);
                    char speed_str[16];
                    rt_snprintf(speed_str, sizeof(speed_str), "%d%%", sensor_msg.data.actor.value);
                    tjc_send_txt("actuator.fan_lav", "txt", speed_str);
                    break;
                }
                case ACTUATOR_PUMP:  // 水泵
                {
                    const char *state_str = sensor_msg.data.actor.state ? "已启动" : "已停止";
                    tjc_show_chinese("actuator.pump", state_str);
                    tjc_send_val("actuator.pum_statu", "val", sensor_msg.data.actor.state);
                    tjc_send_val("actuator.pum_mode", "val", sensor_msg.data.actor.work_mode);
                    break;
                }
                case ACTUATOR_LED:   // LED 照明
                {
                    const char *state_str = sensor_msg.data.actor.state ? "已启动" : "已停止";
                    tjc_show_chinese("actuator.led", state_str);
                    tjc_send_val("actuator.led_statu", "val", sensor_msg.data.actor.state);
                    tjc_send_val("actuator.led_mode", "val", sensor_msg.data.actor.work_mode);
                    tjc_send_val("actuator.led_brightness", "val", sensor_msg.data.actor.value);
                    char bright_str[16];
                    rt_snprintf(bright_str, sizeof(bright_str), "%d%%", sensor_msg.data.actor.value);
                    tjc_send_txt("actuator.led_val", "txt", bright_str);
                    break;
                }
                case ACTUATOR_BUZZER: // 蜂鸣器
                {
                    const char *state_str = sensor_msg.data.actor.state ? "已启动" : "已停止";
                    tjc_show_chinese("actuator.buzzer", state_str);
                    tjc_send_val("actuator.buz_statu", "val", sensor_msg.data.actor.state);
                    tjc_send_val("actuator.buz_mode", "val", sensor_msg.data.actor.work_mode);
                    break;
                }
                case ACTUATOR_ATOMIZ: // 雾化器
                {
                    const char *state_str = sensor_msg.data.actor.state ? "已启动" : "已停止";
                    tjc_show_chinese("actuator.atomiz", state_str);
                    tjc_send_val("actuator.ato_statu", "val", sensor_msg.data.actor.state);
                    tjc_send_val("actuator.ato_mode", "val", sensor_msg.data.actor.work_mode);
                    break;
                }
                case ACTUATOR_AUDIO: // 音频播放
                {
                    const char *state_str = sensor_msg.data.actor.state ? "已启动" : "已停止";
                    tjc_show_chinese("actuator.audio", state_str);
                    tjc_send_val("actuator.aud_statu", "val", sensor_msg.data.actor.state);
                    tjc_send_val("actuator.aud_mode", "val", sensor_msg.data.actor.work_mode);
                    break;
                }

                case SENSOR_AI:{
                    // 显示AI识别结果
                    const char *type_str = "未知";
                    switch (sensor_msg.data.ai.type) {
                    case MUSHROOM_TYPE_HONGGU: type_str = "红菇"; break;
                    case MUSHROOM_TYPE_LANGU:  type_str = "蓝菇"; break;
                    case MUSHROOM_TYPE_LVGU:   type_str = "绿菇"; break;
                    case MUSHROOM_TYPE_YUNGU:  type_str = "云菇"; break;
                    case MUSHROOM_TYPE_HUANGGU:type_str = "黄菇"; break;
                    default: type_str = "未知"; break;
                    }

                    tjc_show_chinese("sensor.type", type_str);
                    break;
                }

                case SENSOR_TIME: {
                    char time_str1[16];
                    rt_snprintf(time_str1, sizeof(time_str1), "%04d-%02d-%02d",
                                sensor_msg.data.time.year,
                                sensor_msg.data.time.month,
                                sensor_msg.data.time.day);
                    tjc_send_txt("sensor.time1", "txt", time_str1);   // 控件名可根据实际屏修改
                    tjc_send_txt("actuator.time1", "txt", time_str1);   // 控件名可根据实际屏修改
                    tjc_send_txt("weather.time1", "txt", time_str1);   // 控件名可根据实际屏修改
                    char time_str2[16];
                    rt_snprintf(time_str2, sizeof(time_str2), "%02d:%02d:%02d",
                                sensor_msg.data.time.hours,
                                sensor_msg.data.time.minutes,
                                sensor_msg.data.time.seconds);
                    tjc_send_txt("sensor.time2", "txt", time_str2);   // 控件名可根据实际屏修改
                    tjc_send_txt("actuator.time2", "txt", time_str2);   // 控件名可根据实际屏修改
                    tjc_send_txt("weather.time2", "txt", time_str2);   // 控件名可根据实际屏修改
                    break;
                }
                default:
                    LOG_D("Unknown sensor type: %d", sensor_msg.type);
                    break;
            }

        }
        else
        {
            LOG_E("Failed to receive from display queue: %d (queue: 0x%08x)",
                  result, (rt_uint32_t)display_queue);
                   LOG_E("Failed to receive from display queue: %d", result);
                   rt_thread_mdelay(100);  // 错误时等待100ms再重试
        }
    }

    rt_thread_mdelay(THREAD_COMM_TICK);
}


/* ==================== 云平台函数  ==================== */
/**
 * @brief 上传线程入口
 * @param parameter 未使用
 *
 * 以固定周期从 upload_queue 中获取每种传感器的最新值并上传至 OneNET。
 * 每次周期内清空队列，只保留每种传感器类型的最后一个值，然后一并上传。
 */
static void process_upload_data(void)
{
    //LOG_I("Upload thread started");
    /* ==================== 固定周期上传 ==================== */
    /* 1：检查 OneNET 连接状态 */
    if (!onenet_mqtt_connected())
    {
        LOG_W("OneNET not connected, waiting for reconnection...");
        rt_thread_mdelay(RECONNECT_CHECK_MS);
        return;   /* 跳过本次上传，继续下一轮循环 */
    }

    /* 3. 初始化各传感器最新值存储 */
    /* 基础环境 */
    double last_air_temperature = 0.0;      // 空气温度
    double last_air_humidity = 0.0;         // 空气湿度
    int32_t last_light_intensity = 0;       // 光照强度 (Lux)
    int32_t last_co2_concentration = 0;     // CO2浓度 (ppm)
    int32_t last_tvoc_concentration = 0;    // TVOC浓度 (ppb)
    int32_t last_soil_moisture = 0;         // 土壤湿度 (%)
    double last_soil_temperature = 0.0;     // 土壤温度 (℃)

    /* AI识别 */
    int32_t last_mushroom_type = 0;         // 蘑菇种类
    double last_recognition_confidence = 0.0; // 置信度

    /* 执行器状态缓存 */
    int32_t last_fan_mode = 0, last_fan_speed = 0;
    rt_bool_t last_fan_state = RT_FALSE;
    int32_t last_pump_mode = 0;
    rt_bool_t last_pump_state = RT_FALSE;
    int32_t last_led_mode = 0, last_led_brightness = 0;
    rt_bool_t last_led_state = RT_FALSE;
    rt_bool_t last_buzzer_state = RT_FALSE;
    int32_t last_atomizer_mode = 0;
    rt_bool_t last_atomizer_state = RT_FALSE;
    int32_t last_audio_mode = 0;
    rt_bool_t last_audio_state = RT_FALSE;

    rt_bool_t has_weather = RT_FALSE;

    /* 更新标志 */
    rt_bool_t updated_temp = RT_FALSE;
    rt_bool_t updated_humi = RT_FALSE;
    rt_bool_t updated_light = RT_FALSE;
    rt_bool_t updated_co2 = RT_FALSE;
    rt_bool_t updated_tvoc = RT_FALSE;
    rt_bool_t updated_soil_moist = RT_FALSE;
    rt_bool_t updated_soil_temp = RT_FALSE;
    rt_bool_t updated_ai = RT_FALSE;
    rt_bool_t updated_fan = RT_FALSE, updated_pump = RT_FALSE, updated_led = RT_FALSE;
    rt_bool_t updated_buzzer = RT_FALSE, updated_atomizer = RT_FALSE, updated_audio = RT_FALSE;

    sensor_msg_t sensor_msg;
    int processed = 0;

    /* 4. 在当前周期内阻塞接收消息，直到周期结束 */
    while (processed < MAX_BATCH_SIZE)
    {
        rt_ssize_t result = rt_mq_recv(upload_queue, &sensor_msg,
                                       sizeof(sensor_msg_t), 0);
        if (result <= 0) {
            //LOG_W("rt_mq_recv returned %d (processed=%d)", result, processed);
            break;
        }

        if (result != sizeof(sensor_msg_t)) {
            LOG_E("Incomplete message received, size=%d", result);
            continue;
        }
        processed++;

        if (result > 0)
        {
          //LOG_D("[UPLOAD] Received type=%d", sensor_msg.type);
            if (result != sizeof(sensor_msg_t)) {
                //LOG_E("Incomplete message received, size=%d", result);
                continue;
            }
            switch (sensor_msg.type) {
                case SENSOR_TEMPERATURE:          // 空气温度
                    last_air_temperature = sensor_msg.data.env.temperature;
                    updated_temp = RT_TRUE;
                    break;
                case SENSOR_HUMIDITY:             // 空气湿度
                    last_air_humidity = sensor_msg.data.env.humidity;
                    updated_humi = RT_TRUE;
                    break;
                case SENSOR_LIGHT:                // 光照强度
                    last_light_intensity = (int32_t)(sensor_msg.data.light_lux + 0.5);
                    updated_light = RT_TRUE;
                    break;
                case SENSOR_ECO2:                 // CO2浓度
                    last_co2_concentration = sensor_msg.data.air.eco2;
                    updated_co2 = RT_TRUE;
                    break;
                case SENSOR_TVOC:                 // TVOC浓度
                    last_tvoc_concentration = sensor_msg.data.air.tvoc;
                    updated_tvoc = RT_TRUE;
                    break;
                case SENSOR_SOIL_MOISTURE:        // 土壤湿度
                    last_soil_moisture = sensor_msg.data.soil.humidity_percent;
                    updated_soil_moist = RT_TRUE;
                    break;
                case SENSOR_SOIL_TEMPERATURE:     // 土壤温度
                    last_soil_temperature = sensor_msg.data.soil_temperature;
                    updated_soil_temp = RT_TRUE;
                    break;
                case SENSOR_AI:                   // AI识别结果
                    last_mushroom_type = (int32_t)sensor_msg.data.ai.type;
                    last_recognition_confidence = sensor_msg.data.ai.confidence;
                    updated_ai = RT_TRUE;
                    break;

                case ACTUATOR_FAN:
                    last_fan_mode = sensor_msg.data.actor.work_mode;
                    last_fan_state = sensor_msg.data.actor.state;
                    last_fan_speed = sensor_msg.data.actor.value;
                    updated_fan = RT_TRUE;
                    break;
                case ACTUATOR_PUMP:
                    last_pump_mode = sensor_msg.data.actor.work_mode;
                    last_pump_state = sensor_msg.data.actor.state;
                    updated_pump = RT_TRUE;
                    break;
                case ACTUATOR_LED:
                    last_led_mode = sensor_msg.data.actor.work_mode;
                    last_led_state = sensor_msg.data.actor.state;
                    last_led_brightness = sensor_msg.data.actor.value;
                    updated_led = RT_TRUE;
                    break;
                case ACTUATOR_BUZZER:
                    last_buzzer_state = sensor_msg.data.actor.state;
                    updated_buzzer = RT_TRUE;
                    break;
                case ACTUATOR_ATOMIZ:
                    last_atomizer_mode = sensor_msg.data.actor.work_mode;
                    last_atomizer_state = sensor_msg.data.actor.state;
                    updated_atomizer = RT_TRUE;
                    break;
                case ACTUATOR_AUDIO:
                    last_audio_mode = sensor_msg.data.actor.work_mode;
                    last_audio_state = sensor_msg.data.actor.state;
                    updated_audio = RT_TRUE;
                    break;
                default:
                    break;
            }
        }
        else if (result == -RT_ETIMEOUT)
        {
            LOG_E("thread receive error: %d", result);
            break;   /* 周期结束，退出接收循环 */
        }
        else
        {
            LOG_E("MQ receive error: %d", result);
            rt_thread_mdelay(10);
        }
    }

    // 检查是否有天气更新事件（非阻塞，不等待）
    if (check_event(EVENT_WEATHER_UPLOAD)) {
        // 标记需要上传天气
        has_weather = RT_TRUE;
    }



    /* 5. 使用 cJSON 打包所有有更新的传感器数据 */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        LOG_E("Failed to create cJSON root");
        return;
    }
    cJSON_AddStringToObject(root, "id", "123");   /* 设备ID，与 onenet_mqtt_upload_digit 一致 */
    cJSON *params = cJSON_CreateObject();
    if (!params) {
        cJSON_Delete(root);
        LOG_E("Failed to create cJSON params");
        return;
    }
    cJSON_AddItemToObject(root, "params", params);

    /* 辅助宏：添加数值属性 */
#define ADD_NUMBER_PARAM(key, value) \
    do { \
        cJSON *obj = cJSON_CreateObject(); \
        cJSON_AddNumberToObject(obj, "value", value); \
        cJSON_AddItemToObject(params, key, obj); \
    } while(0)

    /* 辅助宏：添加字符串属性 */
#define ADD_STRING_PARAM(key, str) \
   do { \
        cJSON *obj = cJSON_CreateObject(); \
        cJSON_AddStringToObject(obj, "value", str); \
        cJSON_AddItemToObject(params, key, obj); \
   } while(0)

#define ADD_BOOL_PARAM(key, val) \
    do { \
        cJSON *obj = cJSON_CreateObject(); \
        cJSON_AddItemToObject(obj, "value", cJSON_CreateBool(val)); \
        cJSON_AddItemToObject(params, key, obj); \
    } while(0)

    /* 基础环境属性（仅当本周期有更新时才上传，避免重复发送旧值） */
    if (updated_temp) {
        double rounded = round_to_n(last_air_temperature, 1);
        ADD_NUMBER_PARAM("air_temperature", rounded);
    }
    if (updated_humi) {
        double rounded = round_to_n(last_air_humidity, 1);
        ADD_NUMBER_PARAM("air_humidity", rounded);
    }
    if (updated_light) {
        ADD_NUMBER_PARAM("light_intensity", last_light_intensity);
    }
    if (updated_co2) {
        ADD_NUMBER_PARAM("co2_concentration", last_co2_concentration);
    }
    if (updated_tvoc) {
        ADD_NUMBER_PARAM("tvoc_concentration", last_tvoc_concentration);
    }
    if (updated_soil_moist) {
        ADD_NUMBER_PARAM("soil_moisture", last_soil_moisture);
    }
    if (updated_soil_temp) {
        double rounded = round_to_n(last_soil_temperature, 1);
        ADD_NUMBER_PARAM("soil_temperature", rounded);
    }

    /* 风扇 */
    if (updated_fan) {
        ADD_NUMBER_PARAM("fan_work_mode", last_fan_mode);
        ADD_BOOL_PARAM("fan_state", last_fan_state);
        ADD_NUMBER_PARAM("fan_speed", last_fan_speed);
        LOG_D("[UPLOAD] Received FAN" );
    }
    /* 水泵 */
    if (updated_pump) {
        ADD_NUMBER_PARAM("pump_work_mode", last_pump_mode);
        ADD_BOOL_PARAM("pump_state", last_pump_state);
    }
    /* LED */
    if (updated_led) {
        ADD_NUMBER_PARAM("led_work_mode", last_led_mode);
        ADD_BOOL_PARAM("led_state", last_led_state);
        ADD_NUMBER_PARAM("led_brightness", last_led_brightness);
    }
    /* 蜂鸣器 */
    if (updated_buzzer) {
        ADD_BOOL_PARAM("buzzer_state", last_buzzer_state);
    }
    /* 加湿器 */
    if (updated_atomizer) {
        ADD_NUMBER_PARAM("atomizer_work_mode", last_atomizer_mode);
        ADD_BOOL_PARAM("atomizer_state", last_atomizer_state);
    }
    /* 音频 */
    if (updated_audio) {
        ADD_NUMBER_PARAM("audio_work_mode", last_audio_mode);
        ADD_BOOL_PARAM("audio_state", last_audio_state);
    }

    /* AI识别属性（本周期有更新才上传） */
    if (updated_ai) {
        ADD_NUMBER_PARAM("mushroom_type", last_mushroom_type);
        /* 置信度保留3位小数，符合步长0.001 */
        double conf_rounded = round_to_n(last_recognition_confidence, 3);
        ADD_NUMBER_PARAM("recognition_confidence", conf_rounded);
    }

    /* 天气属性（如果本周期收到新数据或有历史数据，都上传；这里选择只要有历史数据就上传，保证平台能定期收到） */
    if (has_weather) {
        /* 上传三天的数据，使用下标 0=今天，1=明天，2=后天 */
        for (int i = 0; i < 3; i++) {
            char key_date[32], key_code[32], key_high[32], key_low[32];
            char key_wind_dir[32], key_wind_scale[32];
            rt_snprintf(key_date, sizeof(key_date), "weather_date%d", i+1);
            rt_snprintf(key_code, sizeof(key_code), "weather_code_day%d", i+1);
            rt_snprintf(key_high, sizeof(key_high), "weather_high%d", i+1);
            rt_snprintf(key_low, sizeof(key_low), "weather_low%d", i+1);
            rt_snprintf(key_wind_dir, sizeof(key_wind_dir), "weather_wind_dir%d", i+1);
            rt_snprintf(key_wind_scale, sizeof(key_wind_scale), "weather_wind_scale%d", i+1);

            rt_mutex_take(g_weather_mutex, RT_WAITING_FOREVER);
            weather_day_t *day = &g_weather_data.days[i];
            rt_mutex_release(g_weather_mutex);

            /* 日期字符串非空才上传 */
            if (rt_strlen(day->date) > 0) {
                ADD_STRING_PARAM(key_date, day->date);
            }
            /* 天气代码：使用 code_day (白天天气代码) */
            ADD_NUMBER_PARAM(key_code, day->code_day);
            /* 温度 */
            ADD_NUMBER_PARAM(key_high, day->high);
            ADD_NUMBER_PARAM(key_low, day->low);
            /* 风向和风力等级 */
            if (rt_strlen(day->wind_direction) > 0) {
                ADD_STRING_PARAM(key_wind_dir, day->wind_direction);
            }
            if (rt_strlen(day->wind_scale) > 0) {
                ADD_STRING_PARAM(key_wind_scale, day->wind_scale);
            }
        }
    }


    /* 6. 若有数据，转换为 JSON 字符串并发布到 OneNET */
    if (cJSON_GetArraySize(params) > 0)
    {
        char *json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            net_req_msg_t msg;
            msg.type = NET_REQ_MQTT_PUB;

            /* 复制 JSON 字符串，因为工作线程需要独立内存 */
            size_t len = strlen(json_str);

            msg.data.mqtt_pub.json_str = rt_malloc(len + 1);
            if (msg.data.mqtt_pub.json_str) {
                memcpy(msg.data.mqtt_pub.json_str, json_str, len + 1);
                msg.data.mqtt_pub.json_len = len;
                //LOG_I("JSON size: %zu bytes", strlen(json_str));
                if (rt_mq_send(net_req_queue, &msg, sizeof(msg)) != RT_EOK) {
                    LOG_E("Failed to send MQTT publish request to net worker");
                    rt_free(msg.data.mqtt_pub.json_str);
                }
            } else {
                LOG_E("Failed to allocate memory for JSON string");
            }
            cJSON_free(json_str);
        } else {
            LOG_E("Failed to print JSON");
        }
    }

    cJSON_Delete(root);
    rt_thread_mdelay(THREAD_UPLOAD_TICK);
}

/* ==================== 天气获取函数  ==================== */
static void fetch_weather_and_display(void)
{
    struct webclient_session *session = RT_NULL;
    char *response_buf = RT_NULL;
    size_t total_size = 0;
    int resp_status, bytes_read;
    const int recv_bufsz = 512;
    char temp_buf[recv_bufsz];

    session = webclient_session_create(1024);
    if (session == RT_NULL) {
        LOG_E("No memory for webclient session");
        goto __exit;
    }

    resp_status = webclient_get(session, WEATHER_URL);
    if (resp_status != 200) {
        LOG_E("HTTP GET failed, response: %d", resp_status);
        goto __exit;
    }

    do {
        bytes_read = webclient_read(session, temp_buf, sizeof(temp_buf) - 1);
        if (bytes_read <= 0) break;
        response_buf = rt_realloc(response_buf, total_size + bytes_read + 1);
        if (response_buf == RT_NULL) {
            LOG_E("No memory to expand response buffer");
            goto __exit;
        }
        memcpy(response_buf + total_size, temp_buf, bytes_read);
        total_size += bytes_read;
    } while (1);

    if (response_buf == RT_NULL) {
        LOG_E("No data received");
        goto __exit;
    }
    response_buf[total_size] = '\0';

    cJSON *root = cJSON_Parse(response_buf);
    if (root == RT_NULL) {
        LOG_E("JSON parse error");
        goto __exit;
    }

    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (!results || !cJSON_IsArray(results)) {
        LOG_E("No results array");
        cJSON_Delete(root);
        goto __exit;
    }

    cJSON *first = results->child;
    if (!first) {
        cJSON_Delete(root);
        goto __exit;
    }

    cJSON *daily = cJSON_GetObjectItem(first, "daily");
    if (!daily || !cJSON_IsArray(daily)) {
        LOG_E("No daily array");
        cJSON_Delete(root);
        goto __exit;
    }

    weather_3day_t weather_data;
    memset(&weather_data, 0, sizeof(weather_3day_t));

    int day_idx = 0;
    cJSON *day_obj = daily->child;
    while (day_obj && day_idx < 3) {
        weather_day_t *day = &weather_data.days[day_idx];
        LOG_D("[WEATHER] Parsing day %d", day_idx);

        /* 日期 */
        cJSON *date = cJSON_GetObjectItem(day_obj, "date");
        if (date && date->valuestring) {
            rt_strncpy(day->date, date->valuestring, sizeof(day->date) - 1);
            LOG_D("[WEATHER]   date: %s", day->date);
        } else {
            LOG_W("[WEATHER]   missing date");
        }

        /* 白天天气文字 */
        cJSON *text_day = cJSON_GetObjectItem(day_obj, "text_day");
        if (text_day && text_day->valuestring) {
            rt_strncpy(day->text_day, text_day->valuestring, sizeof(day->text_day) - 1);
            LOG_D("[WEATHER]   text_day: %s", day->text_day);
        } else {
            LOG_W("[WEATHER]   missing text_day");
        }

        /* 白天天气代码 */
        cJSON *code_day = cJSON_GetObjectItem(day_obj, "code_day");
        if (code_day && code_day->valuestring) {
            day->code_day = atoi(code_day->valuestring);
            LOG_D("[WEATHER]   code_day: %d", day->code_day);
        } else {
            LOG_W("[WEATHER]   missing code_day");
        }

        /* 夜间天气文字 */
        cJSON *text_night = cJSON_GetObjectItem(day_obj, "text_night");
        if (text_night && text_night->valuestring) {
            rt_strncpy(day->text_night, text_night->valuestring, sizeof(day->text_night) - 1);
            LOG_D("[WEATHER]   text_night: %s", day->text_night);
        } else {
            LOG_W("[WEATHER]   missing text_night");
        }

        /* 夜间天气代码 */
        cJSON *code_night = cJSON_GetObjectItem(day_obj, "code_night");
        if (code_night && code_night->valuestring) {
            day->code_night = atoi(code_night->valuestring);
            LOG_D("[WEATHER]   code_night: %d", day->code_night);
        } else {
            LOG_W("[WEATHER]   missing code_night");
        }

        /* 最高温度 */
        cJSON *high = cJSON_GetObjectItem(day_obj, "high");
        if (high && high->valuestring) {
            day->high = atoi(high->valuestring);
            LOG_D("[WEATHER]   high: %d", day->high);
        } else {
            LOG_W("[WEATHER]   missing high");
        }

        /* 最低温度 */
        cJSON *low = cJSON_GetObjectItem(day_obj, "low");
        if (low && low->valuestring) {
            day->low = atoi(low->valuestring);
            LOG_D("[WEATHER]   low: %d", day->low);
        } else {
            LOG_W("[WEATHER]   missing low");
        }

        /* 风向 */
        cJSON *wind_dir = cJSON_GetObjectItem(day_obj, "wind_direction");
        if (wind_dir && wind_dir->valuestring) {
            rt_strncpy(day->wind_direction, wind_dir->valuestring, sizeof(day->wind_direction) - 1);
            LOG_D("[WEATHER]   wind_direction: %s", day->wind_direction);
        } else {
            LOG_W("[WEATHER]   missing wind_direction");
        }

        /* 风力等级 */
        cJSON *wind_scale = cJSON_GetObjectItem(day_obj, "wind_scale");
        if (wind_scale && wind_scale->valuestring) {
            rt_strncpy(day->wind_scale, wind_scale->valuestring, sizeof(day->wind_scale) - 1);
            LOG_D("[WEATHER]   wind_scale: %s", day->wind_scale);
        } else {
            LOG_W("[WEATHER]   missing wind_scale");
        }

        day_obj = day_obj->next;
        day_idx++;
    }

    LOG_I("[WEATHER] Parsed %d days of weather data", day_idx);


    /* 将解析好的天气数据存入全局变量（加锁保护） */
    if (g_weather_mutex != RT_NULL) {

        rt_mutex_take(g_weather_mutex, RT_WAITING_FOREVER);

        memcpy(&g_weather_data, &weather_data, sizeof(weather_3day_t));

        rt_mutex_release(g_weather_mutex);

        /* 分别通知三个线程 */
        post_event(EVENT_WEATHER_UPLOAD);
        post_event(EVENT_WEATHER_DISPLAY);

        LOG_I("Weather data updated (3 days) and event sent");
    } else {
        LOG_W("Weather mutex not available, cannot store weather data");
    }

    cJSON_Delete(root);

__exit:
    if (session) webclient_close(session);
    if (response_buf) rt_free(response_buf);
    LOG_D("[WEATHER] Fetch finished");
}

static void update_weather_display(void)
{
    if (g_weather_mutex == RT_NULL) {
        LOG_W("Weather mutex not available");
        return;
    }

    /* 复制天气数据，避免长时间持有互斥锁 */
    weather_3day_t weather_copy;
    rt_mutex_take(g_weather_mutex, RT_WAITING_FOREVER);
    memcpy(&weather_copy, &g_weather_data, sizeof(weather_3day_t));
    rt_mutex_release(g_weather_mutex);

    /* 检查数据有效性（第一天的日期非空即可） */
    if (strlen(weather_copy.days[0].date) == 0) {
        LOG_W("No valid weather data to display");
        return;
    }

    char buf[128];
    /* ==================== 今天 ==================== */
    weather_day_t *today = &weather_copy.days[0];
    /* 日期 */
    tjc_send_txt("weather.date1", "txt", today->date);
    tjc_send_val("weather.code1", "val", today->code_day);

    /* 温度范围 */
    rt_snprintf(buf, sizeof(buf), "%d~%d C", today->low, today->high);
    tjc_show_chinese("weather.temp1", buf);   // 包含摄氏度符号（已特殊处理）

    // 风向风力合并显示（例如 "东北 4级"）
    rt_snprintf(buf, sizeof(buf), "%s风  %s级", today->wind_direction, today->wind_scale);
    tjc_show_chinese("weather.wind1", buf);

    /* ==================== 明天 ==================== */
    weather_day_t *tomorrow = &weather_copy.days[1];
    tjc_send_txt("weather.date2", "txt", tomorrow->date);
    tjc_send_val("weather.code2", "val", tomorrow->code_day);

    /* 温度范围 */
    rt_snprintf(buf, sizeof(buf), "%d~%d C", tomorrow->low, tomorrow->high);
    tjc_show_chinese("weather.temp2", buf);   // 包含摄氏度符号（已特殊处理）

    // 风向风力合并显示（例如 "东北 4级"）
    rt_snprintf(buf, sizeof(buf), "%s风  %s级", tomorrow->wind_direction, tomorrow->wind_scale);
    tjc_show_chinese("weather.wind2", buf);

    /* ==================== 后天 ==================== */
    weather_day_t *dayafter = &weather_copy.days[2];
    tjc_send_txt("weather.date3", "txt", dayafter->date);
    tjc_send_val("weather.code3", "val", dayafter->code_day);

    /* 温度范围 */
    rt_snprintf(buf, sizeof(buf), "%d~%d C", dayafter->low, dayafter->high);
    tjc_show_chinese("weather.temp3", buf);   // 包含摄氏度符号（已特殊处理）

    // 风向风力合并显示（例如 "东北 4级"）
    rt_snprintf(buf, sizeof(buf), "%s风  %s级", dayafter->wind_direction, dayafter->wind_scale);
    tjc_show_chinese("weather.wind3", buf);

    LOG_I("3-day weather display updated (explicit)");
}

/* ==================== 网络工作线程入口 ==================== */
static void net_worker_thread_entry(void *parameter)
{
    net_req_msg_t msg;
    rt_err_t result;

    LOG_I("Network worker thread started");

    while (net_worker_running)
    {
        /* 阻塞等待网络请求 */
        result = rt_mq_recv(net_req_queue, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (result <= 0) {
            LOG_W("net_req_queue receive failed");
            continue;
        }

        /* 根据请求类型执行相应网络操作 */
        switch (msg.type) {
            case NET_REQ_WEATHER:{
                fetch_weather_and_display();   // 内部使用 webclient
                break;
            }
            case NET_REQ_NTP:{
                time_t cur_time = ntp_sync_to_rtc(RT_NULL);
                LOG_D("[NW] NTP result: %ld", cur_time);
                if (cur_time > 0) {
                    LOG_I("NTP sync success");
                } else {
                    LOG_W("NTP sync failed");
                }
                break;
            }
            case NET_REQ_MQTT_PUB:{
                if (msg.data.mqtt_pub.json_str) {
                    int ret = onenet_mqtt_publish(ONENET_TOPIC_DP,
                                                  (uint8_t *)msg.data.mqtt_pub.json_str,
                                                  msg.data.mqtt_pub.json_len);
                    if (ret != RT_EOK) {
                        LOG_E("Failed to publish batch data");
                    }
                    //LOG_I("Publishing JSON: %s", msg.data.mqtt_pub.json_str);  // 添加此行
                    rt_thread_mdelay(500);
                    rt_free(msg.data.mqtt_pub.json_str);  // 释放动态分配的 JSON 字符串
                }
                break;
            }
            case NET_REQ_CMD_RSP:
                /* 处理云平台下发的命令 */
                if (msg.data.cmd_rsp.data && msg.data.cmd_rsp.len > 0) {
                    handle_onenet_command(msg.data.cmd_rsp.data, msg.data.cmd_rsp.len);
                    rt_free(msg.data.cmd_rsp.data);
                } else {
                    LOG_W("Invalid command data in worker");
                }
                break;
        }
    }

    LOG_I("Network worker thread stopped");
}

/* ==================== 通讯线程函数 ==================== */
static void comm_thread_entry(void *parameter)
{
    LOG_I("Communication thread started");

    while (comm_running)
    {
        /* 1. 处理接收到的串口命令 */
        process_received_commands();
        process_display_thread();

        // 检查是否有天气更新事件（非阻塞，不等待）
        if (check_event(EVENT_WEATHER_DISPLAY)) {
            update_weather_display();
        }

    }

    LOG_I("Communication thread stopped");
}

static void upload_thread_entry(void *parameter)
{
    LOG_I("upload thread started");

    while (upload_running)
    {
        /* 1. 处理接收到的串口命令 */
        process_upload_data();
    }

    LOG_I("upload thread stopped");
}

static void weather_thread_entry(void *parameter)
{
    net_req_msg_t msg;
    LOG_I("Weather thread started");
    rt_thread_mdelay(10000);

    while (weather_running) {
        /* 发送天气请求到网络工作线程 */
        msg.type = NET_REQ_WEATHER;
        if (rt_mq_send(net_req_queue, &msg, sizeof(net_req_msg_t)) != RT_EOK) {
            LOG_E("Failed to send weather request to net worker");
            //continue;
        }
        /* 等待下一个周期 */
        rt_thread_mdelay(WEATHER_INTERVAL_MS);
    }

    LOG_I("Weather thread stopped");
}

/* ==================== NTP线程入口 ==================== */
static void ntp_thread_entry(void *parameter)
{
    net_req_msg_t msg;
    LOG_I("NTP thread started");
    rt_thread_mdelay(10000);
    while (ntp_running)
    {
        msg.type = NET_REQ_NTP;
        if (rt_mq_send(net_req_queue, &msg, sizeof(net_req_msg_t)) != RT_EOK) {
            LOG_E("Failed to send NTP request to net worker");
            //continue;
        }
        /* 等待同步间隔 */
        rt_thread_mdelay(NTP_SYNC_INTERVAL_MS);
    }

    LOG_I("NTP thread stopped");
}

/* ==================== 通讯线程初始化 ==================== */
int comm_thread_init(void)
{
    LOG_I("Starting communication thread...");

    if (onenet_mqtt_init() != RT_EOK)
    {
        LOG_E("onenet thread initialization failed");
    }
    else
    {
        LOG_I("onenet mqtt init success");

        /* 注册统一的命令响应回调函数，替代之前的订阅方式 */
        onenet_set_cmd_rsp_cb(onenet_cmd_rsp_cb);
        LOG_I("onenet cmd rsp callback registered");
    }

    /* 初始化TJC串口屏 */
    if (tjc_uart_init() != RT_EOK)
    {
        LOG_W("TJC UART init failed! Display may not work properly");
    }

    comm_running = RT_TRUE;
    upload_running = RT_TRUE;
    weather_running = RT_TRUE;
    ntp_running = RT_TRUE;
    net_worker_running = RT_TRUE;

    /* 1. 创建网络请求队列 */
    net_req_queue = rt_mq_create("net_req_q",
        sizeof(net_req_msg_t),
        NET_REQ_QUEUE_SIZE,
        RT_IPC_FLAG_FIFO);
    if (net_req_queue == RT_NULL) {
        LOG_E("Failed to create net request queue");
        return -RT_ERROR;
    }

    /* 创建通讯线程 */
    comm_thread = rt_thread_create("comm",
        comm_thread_entry,
        RT_NULL,
        THREAD_COMM_STACK_SIZE,
        THREAD_COMM_PRIORITY,
        10);
    if (comm_thread == RT_NULL)
    {
        LOG_E("Failed to create communication thread");
        return -RT_ERROR;
    }

    /* 创建上传线程 */
    upload_thread = rt_thread_create("upload",
        upload_thread_entry,
        RT_NULL,
        THREAD_UPLOAD_STACK_SIZE,      // 使用上传线程专用栈大小
        THREAD_UPLOAD_PRIORITY,        // 使用上传线程专用优先级
        10);
    if (upload_thread == RT_NULL)
    {
        LOG_E("Failed to create upload thread");
        /* 可考虑停止通讯线程并返回错误，此处简单清理后返回 */
        rt_thread_delete(comm_thread);
        comm_thread = RT_NULL;
        return -RT_ERROR;
    }

    /* 创建天气线程 */
    weather_thread = rt_thread_create("weather",
            weather_thread_entry,
            RT_NULL,
            THREAD_WEATHER_STACK_SIZE,
            THREAD_WEATHER_PRIORITY,
            10);
    if (weather_thread == RT_NULL)
    {
        LOG_E("Failed to create weather thread");
        rt_thread_delete(comm_thread);
        rt_thread_delete(upload_thread);
        return -RT_ERROR;
    }

    g_weather_mutex = rt_mutex_create("weather_mtx",
                                       RT_IPC_FLAG_FIFO);
    if (g_weather_mutex == RT_NULL) {
        LOG_E("Failed to create weather mutex");
        // 错误处理
    }

    /* 创建NTP线程 */
    ntp_thread = rt_thread_create("ntp",
            ntp_thread_entry,
            RT_NULL,
            THREAD_NTP_STACK_SIZE,
            THREAD_NTP_PRIORITY,
            10);
    if (ntp_thread == RT_NULL)
    {
        LOG_E("Failed to create NTP thread");
        /* 清理已创建的线程 */
        rt_thread_delete(comm_thread);
        rt_thread_delete(upload_thread);
        rt_thread_delete(weather_thread);
        return -RT_ERROR;
    }

    net_worker_thread = rt_thread_create("net_worker",
            net_worker_thread_entry,
            RT_NULL,
            NET_WORKER_STACK_SIZE,
            NET_WORKER_PRIORITY,
            10);
    if (net_worker_thread == RT_NULL) {
        LOG_E("Failed to create network worker thread");
        rt_mq_delete(net_req_queue);
        rt_thread_delete(net_worker_thread);
        return -RT_ERROR;
    }

    /* 启动所有线程 */
    rt_thread_startup(comm_thread);
    rt_thread_startup(upload_thread);
    rt_thread_startup(weather_thread);
    rt_thread_startup(ntp_thread);
    rt_thread_startup(net_worker_thread);

    LOG_I("Communication and upload threads started successfully");
    return RT_EOK;
}

/* ==================== 系统清理函数 ==================== */
void system_cleanup(void)
{
    LOG_I("Stopping all system threads...");

    /* 停止通讯线程 */
    comm_running = RT_FALSE;
    if (comm_thread != RT_NULL)
    {
        rt_thread_delete(comm_thread);
        comm_thread = RT_NULL;
    }

    /* 停止上传线程 */
    upload_running = RT_FALSE;
    if (upload_thread != RT_NULL)
    {
        rt_thread_delete(upload_thread);
        upload_thread = RT_NULL;
    }

    /* 停止天气线程 */
    weather_running = RT_FALSE;
    if (weather_thread != RT_NULL) {
        rt_thread_delete(weather_thread);
        weather_thread = RT_NULL;
    }

    /* 停止NTP线程 */
    ntp_running = RT_FALSE;
    if (ntp_thread != RT_NULL) {
        rt_thread_delete(ntp_thread);
        ntp_thread = RT_NULL;
    }

    /* 停止网络工作线程 */
    net_worker_running = RT_FALSE;
    if (net_worker_thread != RT_NULL) {
        rt_thread_delete(net_worker_thread);
        net_worker_thread = RT_NULL;
    }
    if (net_req_queue != RT_NULL) {
        rt_mq_delete(net_req_queue);
        net_req_queue = RT_NULL;
    }

    /* 停止传感器线程 */
    sensors_threads_stop();

    /* 停止执行器线程 */
    actuators_threads_stop();


    LOG_I("All threads stopped");
}

/* ==================== OneNET 命令响应回调（处理小程序下发） ==================== */
static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size,
                              uint8_t **resp_data, size_t *resp_size)
{
    /* 关键：不在此函数中返回任何响应，全部交给工作线程处理 */
    *resp_data = NULL;
    *resp_size = 0;

    /* 如果没有接收到数据，直接返回 */
    if (recv_data == RT_NULL || recv_size == 0) {
        LOG_W("Empty command received");
        return;
    }

    /* 拷贝原始数据，发送给网络工作线程处理 */
    uint8_t *data_copy = rt_malloc(recv_size);
    if (data_copy == RT_NULL) {
        LOG_E("Failed to allocate memory for command data");
        return;
    }
    memcpy(data_copy, recv_data, recv_size);

    net_req_msg_t msg;
    msg.type = NET_REQ_CMD_RSP;
    msg.data.cmd_rsp.data = data_copy;
    msg.data.cmd_rsp.len = recv_size;

    if (rt_mq_send(net_req_queue, &msg, sizeof(msg)) != RT_EOK) {
        LOG_E("Failed to send command to net worker");
        rt_free(data_copy);
    }
}


static void handle_onenet_command(uint8_t *recv_data, size_t recv_size)
{
    if (recv_data == RT_NULL || recv_size == 0) {
        LOG_W("Empty command received");
        return;
    }

    LOG_D("recv data: %.*s", (int)recv_size, recv_data);

    cJSON *root = cJSON_Parse((const char*)recv_data);
    if (root == RT_NULL) {
        LOG_E("Parse JSON failed");
        return;
    }

    /* 提取命令 id（必须原样返回） */
    char id_str[32] = "";
    cJSON *id_obj = cJSON_GetObjectItem(root, "id");
    if (id_obj && cJSON_IsString(id_obj)) {
        rt_strncpy(id_str, id_obj->valuestring, sizeof(id_str) - 1);
    }

    /* 提取 params 对象 */
    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params == RT_NULL || !cJSON_IsObject(params)) {
        LOG_W("No params object in command");
        cJSON_Delete(root);
        return;
    }

    /* ---------- 判断是否为服务调用（包含 type 字段） ---------- */
    cJSON *type_item = cJSON_GetObjectItem(params, "type");
    if (type_item != RT_NULL && cJSON_IsNumber(type_item)) {
        /* ====== 服务调用分支 ====== */
        int type = type_item->valueint;
        int state = 0, mode = 0, value = 0;

        cJSON *state_item = cJSON_GetObjectItem(params, "state");
        if (state_item && cJSON_IsNumber(state_item)) state = state_item->valueint;

        cJSON *mode_item = cJSON_GetObjectItem(params, "mode");
        if (mode_item && cJSON_IsNumber(mode_item)) mode = mode_item->valueint;

        cJSON *value_item = cJSON_GetObjectItem(params, "value");
        if (value_item && cJSON_IsNumber(value_item)) value = value_item->valueint;

        LOG_I("Cloud service call: type=%d, state=%d, mode=%d, value=%d", type, state, mode, value);

        /* 构造命令结构体并发送到对应的执行器邮箱 */
        command_t cmd;
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.source = CLOUD;
        cmd.timestamp = rt_tick_get();

        switch (type) {
            case 1: /* 风扇 */
                cmd.type = MSG_FAN;
                cmd.data.fan.mode = (mode == 1) ? FAN_AUTO : FAN_MANU;
                cmd.data.fan.fan_status = (state == 1) ? RT_TRUE : RT_FALSE;
                cmd.data.fan.speed = value;
                send_command(fan_mailbox, &cmd);
                LOG_I("Cloud: fan set mode=%d, state=%d, speed=%d", mode, state, value);
                break;

            case 2: /* 水泵 */
                cmd.type = MSG_PUMP;
                cmd.data.pump.mode = (mode == 1) ? PUMP_AUTO : PUMP_MANU;
                cmd.data.pump.pump_status = (state == 1) ? RT_TRUE : RT_FALSE;
                send_command(pump_mailbox, &cmd);
                LOG_I("Cloud: pump set mode=%d, state=%d", mode, state);
                break;

            case 3: /* LED */
                cmd.type = MSG_LED;
                cmd.data.led.mode = (mode == 1) ? LED_AUTO : LED_MANU;
                cmd.data.led.led_status = (state == 1) ? RT_TRUE : RT_FALSE;
                cmd.data.led.brightness = value;
                send_command(led_mailbox, &cmd);
                LOG_I("Cloud: LED set mode=%d, state=%d, brightness=%d", mode, state, value);
                break;

            case 4: /* 蜂鸣器 */
                cmd.type = MSG_BUZZER;
                cmd.data.buzzer.mode = (mode == 1) ? BUZZER_AUTO : BUZZER_MANU;
                cmd.data.buzzer.buzzer_status = (state == 1) ? RT_TRUE : RT_FALSE;
                send_command(buzzer_mailbox, &cmd);
                LOG_I("Cloud: buzzer set mode=%d, state=%d", mode, state);
                break;

            case 5: /* 雾化器 */
                cmd.type = MSG_ATOMIZ;
                cmd.data.atomiz.mode = (mode == 1) ? ATOMIZ_AUTO : ATOMIZ_MANU;
                cmd.data.atomiz.atomiz_status = (state == 1) ? RT_TRUE : RT_FALSE;
                send_command(atomiz_mailbox, &cmd);
                LOG_I("Cloud: atomizer set mode=%d, state=%d", mode, state);
                break;

            case 6: /* 音频 */
                cmd.type = MSG_AUDIO;
                cmd.data.audio.mode = (mode == 1) ? AUDIO_AUTO : AUDIO_MANU;
                cmd.data.audio.audio_status = (state == 1) ? RT_TRUE : RT_FALSE;
                send_command(audio_mailbox, &cmd);
                LOG_I("Cloud: audio set mode=%d, state=%d", mode, state);
                break;

            default:
                LOG_W("Unknown service type: %d", type);
                break;
        }

    }

    /* ---------- 最后：在工作线程中发送正确响应 ---------- */
    char resp_buf[128];
    rt_snprintf(resp_buf, sizeof(resp_buf),
                "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", id_str);
    /* 响应主题：响应topic: $sys/{pid}/{device-name}/thing/service/{identifier}/invoke_reply */
    const char *resp_topic = "$sys/     /     /thing/service/command/invoke_reply";
    if (onenet_mqtt_publish(resp_topic, (uint8_t*)resp_buf, strlen(resp_buf)) != RT_EOK) {
        LOG_E("Failed to send cloud command response");
    } else {
        LOG_D("Cloud command response sent: %s", resp_buf);
    }

    cJSON_Delete(root);

}
