/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * 2026-01-05     Optimized   Multi-thread architecture with communication mechanisms
 */

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG

#include "common.h"
#include "sensors.h"
#include "actuators.h"
#include "ipc_manager.h"
#include "core_process.h"
#include <external_comm.h>
#include <rtdbg.h>
#include <time.h>
#include <board.h>
#include <cm_backtrace.h>

/* 全局变量定义 */
sensor_data_t sensor_data;

static rt_device_t wdt_dev = RT_NULL;

/* 独立看门狗初始化函数 */
static void wdt_init(void)
{
    /* 查找看门狗设备 */
    wdt_dev = rt_device_find("wdt");
    if (wdt_dev == RT_NULL) {
        LOG_E("Watchdog device not found!");
        return;
    }

    /* 启动看门狗（如需设置超时，可在此处添加） */
    rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_START, RT_NULL);
    LOG_I("Watchdog started successfully");
}

/* 喂狗函数（可在心跳线程中调用） */
static void wdt_feed(void)
{
    if (wdt_dev != RT_NULL) {
        rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
    }
}

/* ==================== 心跳线程 ==================== */
static void heartbeat_thread_entry(void *parameter)
{
    LOG_I("Heartbeat thread started");

    /* 初始化看门狗（仅需调用一次） */
    wdt_init();

    while (1)
    {
        /* 喂狗 */
        wdt_feed();

        /* 更新系统状态 */
        lock_data();
        sensor_data.system.uptime = rt_tick_get() / RT_TICK_PER_SECOND;
        sensor_data.system.system_status = 1;
        unlock_data();

        rt_thread_mdelay(SYS_HEARTBEAT_INTERVAL); // 例如 1000ms
    }
}

/* ==================== 系统初始化 ==================== */
static int system_init(void)
{
    LOG_I("=== Multi-Sensor Auto Monitoring System Start ===");
    /*初始化通信机制 */
    if (communication_init() != RT_EOK)
    {
        LOG_E("Communication initialization failed");
        return -RT_ERROR;
    }

    /*初始化队列和邮箱 */
    if (queue_init() != RT_EOK)
    {
        LOG_E("Queue initialization failed");
        return -RT_ERROR;
    }

    if (mailbox_init() != RT_EOK)
    {
        LOG_E("Mailbox initialization failed");
        return -RT_ERROR;
    }

    lock_data();
    /*初始化全局数据结构 */
    memset(&sensor_data, 0, sizeof(sensor_data_t));
    unlock_data();

    /*启动通讯线程（包含显示和命令接收） */
    if (comm_thread_init() != RT_EOK)
    {
        LOG_E("Communication thread initialization failed");
    }

    /*启动核心线程*/
    if (process_threads_init() != RT_EOK)
    {
        LOG_E("process thread initialization failed");
    }

    if (sensors_threads_init() != RT_EOK)
    {
        LOG_E("Sensor threads initialization failed");
        return -RT_ERROR;
    }

    if (actuators_threads_init() != RT_EOK)
    {
        LOG_E("Actuator threads initialization failed");
        return -RT_ERROR;
    }

    /*启动心跳线程 */
    rt_thread_t heartbeat_thread = rt_thread_create("heartbeat",
                                                   heartbeat_thread_entry,
                                                   RT_NULL,
                                                   300,
                                                   5,
                                                   10);
    if (heartbeat_thread != RT_NULL)
    {
        rt_thread_startup(heartbeat_thread);
    }

    LOG_I("=== System initialization completed successfully ===");
    return RT_EOK;
}
/* ==================== 主函数 ==================== */
int main(void)
{
    if (system_init() != RT_EOK)
    {
        LOG_E("System initialization failed");
        return -RT_ERROR;
    }

    cm_backtrace_init("mushroom_house", "V1.0", "V1.0");

    /* 主循环，处理系统级事件 */
    while (1)
    {

        rt_thread_mdelay(1000);  // 主循环1秒一次
    }

    /* 清理资源（通常不会执行到这里） */
    system_cleanup();

    return RT_EOK;

}


//#include "actuators.h"
//
///* 测试命令：播放指定曲目 */
//static void test_audio_play(int argc, char **argv)
//{
//    if (argc < 2) {
//        rt_kprintf("Usage: test_audio_play <track_num>\n");
//        rt_kprintf("Example: test_audio_play 1\n");
//        return;
//    }
//
//    int track = atoi(argv[1]);
//    if (track < 1 || track > 65535) {
//        rt_kprintf("Error: Track number must be between 1 and 65535\n");
//        return;
//    }
//
//    /* 调用 actuators.c 中已实现的播放函数 */
//    audio_play_single_loop(track);
//    rt_kprintf("Playing track %d (single loop mode).\n", track);
//}
//
///* 测试命令：停止播放 */
//static void test_audio_stop(int argc, char **argv)
//{
//    audio_stop();
//    rt_kprintf("Audio stopped.\n");
//}
//
///* 导出到 MSH 命令列表 */
//MSH_CMD_EXPORT(test_audio_play, Test JQ8900: play specified track in single loop mode);
//MSH_CMD_EXPORT(test_audio_stop, Stop audio playback);
//











//extern RTC_HandleTypeDef hrtc;   // 板级支持包中已定义
//
//static void rtc_test(void)
//{
//    RTC_TimeTypeDef rtc_time;
//    RTC_DateTypeDef rtc_date;
//
//    /* 读取 RTC 时间 */
//    if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) {
//        LOG_E("Read RTC time failed");
//        return;
//    }
//    /* 必须读取日期，否则 RTC 锁定 */
//    if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK) {
//        LOG_E("Read RTC date failed");
//        return;
//    }
//
//    rt_kprintf("Current time: %02d:%02d:%02d\r\n",
//               rtc_time.Hours, rtc_time.Minutes, rtc_time.Seconds);
//    rt_kprintf("Current date: %04d-%02d-%02d\r\n",
//               2000 + rtc_date.Year, rtc_date.Month, rtc_date.Date);
//}
//
//int main(void)
//{
//    rt_thread_mdelay(1000);   // 等待系统稳定
//    while(1)
//    {
//        /* 检查 LSE 是否就绪 */
//        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET) {
//            /* 等待 LSE 稳定，若长时间未就绪则可能有问题 */
//            rt_thread_mdelay(10);
//        }
//        if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == SET) {
//            LOG_I("LSE is ready");
//        } else {
//            LOG_E("LSE not ready");
//        }
//
//        uint32_t rtcclk = __HAL_RCC_GET_RTC_SOURCE();
//        if (rtcclk == RCC_RTCCLKSOURCE_LSE) {
//            LOG_I("RTC source is LSE");
//        } else {
//            LOG_E("RTC source is NOT LSE (0x%X)", rtcclk);
//        }
//
//        uint32_t asynch = (RTC->PRER & RTC_PRER_PREDIV_A) >> 16;
//        uint32_t synch = RTC->PRER & RTC_PRER_PREDIV_S;
//        LOG_I("AsynchPrediv=%lu, SynchPrediv=%lu", asynch, synch);
//
//        rtc_test();
//        rt_thread_mdelay(1000);   // 等待系统稳定
//    }
//    return 0;
//}
//
//
//
//
//
//
//
//
//
//
//
//
//
//

//
//
///* ==================== 调试命令 ==================== */
//static void sgp30_debug(int argc, char **argv)
//{
//    rt_kprintf("=== SGP30 Debug Information ===\n");
//    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
//    rt_kprintf("Data Valid: %s\n", sensor_data.sgp30.data_valid ? "Yes" : "No");
//    rt_kprintf("Warmup Progress: %d%%\n", sensor_data.sgp30.warmup_percent);
//    rt_kprintf("Current Data - eCO2: %d ppm, TVOC: %d ppb\n",
//               sensor_data.sgp30.eco2, sensor_data.sgp30.tvoc);
//    rt_mutex_release(data_mutex);
//}
//
//static void sgp30_save_baseline(int argc, char **argv)
//{
//    rt_kprintf("SGP30 baseline save functionality needs hardware implementation\n");
//}
//
//static void sgp30_load_baseline(int argc, char **argv)
//{
//    if (argc != 3)
//    {
//        rt_kprintf("Usage: sgp30_load_baseline <eco2_hex> <tvoc_hex>\n");
//        rt_kprintf("Example: sgp30_load_baseline 8E3A A56B\n");
//        return;
//    }
//    rt_kprintf("SGP30 baseline load functionality needs hardware implementation\n");
//}
//
//static void calibrate_soil(int argc, char **argv)
//{
//    rt_kprintf("Soil calibration functionality needs to be implemented in sensors.c\n");
//}
//
//static void show_calibration(int argc, char **argv)
//{
//    rt_kprintf("Show calibration functionality needs to be implemented in sensors.c\n");
//}
//
//static void set_calib(int argc, char **argv)
//{
//    rt_kprintf("Set calibration functionality needs to be implemented in sensors.c\n");
//}
//
//static void sensor_status(int argc, char **argv)
//{
//    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
//
//    rt_kprintf("=== Sensor Status ===\n");
//    rt_kprintf("Soil Sensors: ");
//
//    rt_kprintf("Soil(%d%%) " ,sensor_data.soil.humidity_percentage);
//    rt_kprintf("\n");
//
//    rt_kprintf("Environment: ");
//    if (sensor_data.bh1750.ready) rt_kprintf("BH1750 ");
//    if (sensor_data.sht3x.ready) rt_kprintf("SHT3X ");
//    if (sensor_data.sgp30.ready)
//    {
//        if (sensor_data.sgp30.data_valid)
//            rt_kprintf("SGP30(Valid) ");
//        else
//            rt_kprintf("SGP30(Warmup %d%%) ", sensor_data.sgp30.warmup_percent);
//    }
//    rt_kprintf("\n");
//
//    rt_kprintf("Fan: %s(%s,%d%%) ",
//               sensor_data.fan.state == FAN_ON ? "ON" :
//               (sensor_data.fan.state == FAN_OFF ? "OFF" : "AUTO"),
//               sensor_data.fan.current_status ? "RUNNING" : "STOPPED",
//               sensor_data.fan.speed);  // 修改这里
//
//    rt_kprintf("Pump: %s(%s) ",
//               sensor_data.pump.state == PUMP_ON ? "ON" :
//               (sensor_data.pump.state == PUMP_OFF ? "OFF" : "AUTO"),
//               sensor_data.pump.current_status ? "RUNNING" : "STOPPED");
//
//    //rt_kprintf("Read Count: %d\n", sensor_data.read_count);
//    rt_mutex_release(data_mutex);
//}
//
///* ==================== 风扇控制命令 ==================== */
//static void fan_control(int argc, char **argv)
//{
//    if (argc != 2)
//    {
//        rt_kprintf("Usage: fan_control <on|off|auto>\n");
//        rt_kprintf("  on   - Turn fan on manually (LOW level)\n");
//        rt_kprintf("  off  - Turn fan off manually (HIGH level)\n");
//        rt_kprintf("  auto - Enable automatic temperature control\n");
//        return;
//    }
//
//    if (rt_strcmp(argv[1], "on") == 0)
//    {
//        fan_set_state(FAN_ON);
//    }
//    else if (rt_strcmp(argv[1], "off") == 0)
//    {
//        fan_set_state(FAN_OFF);
//    }
//    else if (rt_strcmp(argv[1], "auto") == 0)
//    {
//        fan_set_state(FAN_AUTO);
//    }
//    else
//    {
//        rt_kprintf("Invalid parameter. Use: on, off, or auto\n");
//    }
//}
//
//static void fan_status(int argc, char **argv)
//{
//    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
//
//    rt_kprintf("=== Fan Control Status ===\n");
//    rt_kprintf("Current State: %s\n",
//               sensor_data.fan.state == FAN_ON ? "ON" :
//               (sensor_data.fan.state == FAN_OFF ? "OFF" : "AUTO"));
//    rt_kprintf("Current Status: %s\n",
//               sensor_data.fan.current_status ? "RUNNING (LOW)" : "STOPPED (HIGH)");
//    rt_kprintf("Manual Override: %s\n",
//               sensor_data.fan.manual_override ? "Yes" : "No");
//    rt_kprintf("Control Temperature: %.1fC\n", sensor_data.fan.control_temperature);
//    rt_kprintf("Auto Control: Enabled\n");
//    rt_kprintf("Temperature Thresholds: High=28.0C, Low=25.0C\n");
//
//    rt_mutex_release(data_mutex);
//}
//
//static void set_fan_threshold(int argc, char **argv)
//{
//    rt_kprintf("Set fan threshold functionality needs modification in actuators.c\n");
//}
//
//static void set_fan_speed(int argc, char **argv)
//{
//    if (argc != 2)
//    {
//        rt_kprintf("Usage: set_fan_speed <speed_percent>\n");
//        rt_kprintf("Example: set_fan_speed 70 (sets fan to 70%% speed)\n");
//        rt_kprintf("Current fan speed: %d%%\n", sensor_data.fan.speed);
//        return;
//    }
//
//    int speed = atoi(argv[1]);
//    if (speed < 0 || speed > 100)
//    {
//        rt_kprintf("Error: Speed must be between 0 and 100\n");
//        return;
//    }
//
//    fan_set_speed(speed);
//    rt_kprintf("Fan speed set to %d%%\n", speed);
//}
///* ==================== 水泵控制命令 ==================== */
//static void pump_control(int argc, char **argv)
//{
//    if (argc != 2)
//    {
//        rt_kprintf("Usage: pump_control <on|off|auto>\n");
//        rt_kprintf("  on   - Turn pump on manually\n");
//        rt_kprintf("  off  - Turn pump off manually\n");
//        rt_kprintf("  auto - Enable automatic humidity control\n");
//        return;
//    }
//
//    if (rt_strcmp(argv[1], "on") == 0)
//    {
//        pump_set_state(PUMP_ON);
//    }
//    else if (rt_strcmp(argv[1], "off") == 0)
//    {
//        pump_set_state(PUMP_OFF);
//    }
//    else if (rt_strcmp(argv[1], "auto") == 0)
//    {
//        pump_set_state(PUMP_AUTO);
//    }
//    else
//    {
//        rt_kprintf("Invalid parameter. Use: on, off, or auto\n");
//    }
//}
//
//static void pump_status(int argc, char **argv)
//{
//    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
//
//    rt_kprintf("=== Pump Control Status ===\n");
//    rt_kprintf("Current State: %s\n",
//               sensor_data.pump.mode == PUMP_ON ? "ON" :
//               (sensor_data.pump.mode == PUMP_OFF ? "OFF" : "AUTO"));
//    rt_kprintf("Current Status: %s\n",
//               sensor_data.pump.current_status ? "RUNNING" : "STOPPED");
//    rt_kprintf("Control Humidity: %d%%\n", sensor_data.pump.control_humidity);
//    rt_kprintf("Auto Control: Enabled\n");
//    rt_kprintf("Humidity Thresholds: Low=30%%, High=60%%\n");
//
//    rt_mutex_release(data_mutex);
//}
//
//static void set_pump_threshold(int argc, char **argv)
//{
//    rt_kprintf("Set pump threshold functionality needs modification in actuators.c\n");
//}
//
//
///* ==================== LED控制命令 ==================== */
//static void led_control(int argc, char **argv)
//{
//    if (argc != 2)
//    {
//        rt_kprintf("Usage: led_control <off|on|breath|blink>\n");
//        rt_kprintf("  off    - Turn LED off\n");
//        rt_kprintf("  on     - Turn LED on (100%% brightness)\n");
//        rt_kprintf("  breath - Enable breathing mode\n");
//        rt_kprintf("  blink  - Enable blinking mode\n");
//        return;
//    }
//
//    if (rt_strcmp(argv[1], "off") == 0)
//    {
//        led_set_mode(LED_OFF);
//    }
//    else if (rt_strcmp(argv[1], "on") == 0)
//    {
//        led_set_mode(LED_ON);
//    }
//    else if (rt_strcmp(argv[1], "LED_AUTO") == 0)
//    {
//        led_set_mode(LED_AUTO);
//    }
//    else
//    {
//        rt_kprintf("Invalid parameter. Use: off, on, LED_AUTO\n");
//    }
//}
//
//static void led_status(int argc, char **argv)
//{
//    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
//
//    rt_kprintf("=== LED Control Status ===\n");
//    rt_kprintf("Current Mode: %s\n",
//               sensor_data.led.mode == LED_OFF ? "OFF" :
//               (sensor_data.led.mode == LED_ON ? "ON" :
//                (sensor_data.led.mode == LED_AUTO ? "LED_AUTO" : "LED_AUTO")));
//    rt_kprintf("Current Brightness: %d%%\n", sensor_data.led.brightness);
//    rt_kprintf("Breath Direction: %s\n",
//               sensor_data.led.breath_dir ? "Increasing" : "Decreasing");
//
//    rt_mutex_release(data_mutex);
//}
//
//
///* ==================== 蜂鸣器控制命令 ==================== */
//static void buzzer_control(int argc, char **argv)
//{
//    if (argc != 2)
//    {
//        rt_kprintf("Usage: buzzer_control <off|on|beep|alarm>\n");
//        rt_kprintf("  off    - Turn buzzer off\n");
//        rt_kprintf("  on     - Turn buzzer on continuously\n");
//        rt_kprintf("  beep   - Enable beeping mode (间隔蜂鸣)\n");
//        rt_kprintf("  alarm  - Enable alarm mode (快速蜂鸣)\n");
//        return;
//    }
//
//    if (rt_strcmp(argv[1], "off") == 0)
//    {
//        buzzer_set_state(BUZZER_OFF);
//    }
//    else if (rt_strcmp(argv[1], "on") == 0)
//    {
//        buzzer_set_state(BUZZER_ON);
//    }
//    else if (rt_strcmp(argv[1], "beep") == 0)
//    {
//        buzzer_set_state(BUZZER_BEEP);
//    }
//    else if (rt_strcmp(argv[1], "alarm") == 0)
//    {
//        buzzer_set_state(BUZZER_ALARM);
//    }
//    else
//    {
//        rt_kprintf("Invalid parameter. Use: off, on, beep, or alarm\n");
//    }
//}
//
//static void buzzer_status(int argc, char **argv)
//{
//    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
//
//    rt_kprintf("=== Buzzer Control Status ===\n");
//    rt_kprintf("Current State: %s\n",
//               sensor_data.buzzer.mode == BUZZER_OFF ? "OFF" :
//               (sensor_data.buzzer_state == BUZZER_ON ? "ON" :
//                (sensor_data.buzzer_state == BUZZER_BEEP ? "BEEP" : "ALARM")));
//    rt_kprintf("Current Status: %s\n",
//               sensor_data.buzzer_current_status ? "BEEPING (LOW)" : "SILENT (HIGH)");
//    rt_kprintf("Beep Active: %s\n",
//               sensor_data.buzzer_beep_active ? "Yes" : "No");
//    rt_kprintf("Last Change: %d ticks ago\n",
//               rt_tick_get() - sensor_data.buzzer_last_change);
//
//    if (sensor_data.buzzer_beep_active)
//    {
//        rt_kprintf("Beep Duration: %d ms\n", sensor_data.buzzer_beep_duration);
//        rt_kprintf("Beep Interval: %d ms\n", sensor_data.buzzer_beep_interval);
//    }
//
//    rt_mutex_release(data_mutex);
//}
//
//static void buzzer_beep_cmd(int argc, char **argv)
//{
//    if (argc != 2)
//    {
//        rt_kprintf("Usage: buzzer_beep <duration_ms>\n");
//        rt_kprintf("Example: buzzer_beep 500 (蜂鸣500毫秒)\n");
//        return;
//    }
//
//    int duration = atoi(argv[1]);
//    if (duration <= 0 || duration > 5000)
//    {
//        rt_kprintf("Error: Duration must be between 1 and 5000 ms\n");
//        return;
//    }
//
//    buzzer_beep(duration);
//    rt_kprintf("Buzzer beeped for %d ms\n", duration);
//}
//
//static void buzzer_alarm_cmd(int argc, char **argv)
//{
//    if (argc != 4)
//    {
//        rt_kprintf("Usage: buzzer_alarm <beep_ms> <interval_ms> <count>\n");
//        rt_kprintf("Example: buzzer_alarm 100 200 5 (蜂鸣5次，每次100ms，间隔200ms)\n");
//        return;
//    }
//
//    int beep_ms = atoi(argv[1]);
//    int interval_ms = atoi(argv[2]);
//    int count = atoi(argv[3]);
//
//    if (beep_ms <= 0 || beep_ms > 1000)
//    {
//        rt_kprintf("Error: Beep duration must be between 1 and 1000 ms\n");
//        return;
//    }
//    if (interval_ms < 0 || interval_ms > 2000)
//    {
//        rt_kprintf("Error: Interval must be between 0 and 2000 ms\n");
//        return;
//    }
//    if (count <= 0 || count > 20)
//    {
//        rt_kprintf("Error: Count must be between 1 and 20\n");
//        return;
//    }
//
//    buzzer_alarm(beep_ms, interval_ms, count);
//    rt_kprintf("Alarm completed: %d beeps\n", count);
//}
//
///* 导出到MSH命令 */
//MSH_CMD_EXPORT(calibrate_soil, Calibrate soil moisture sensor);
//MSH_CMD_EXPORT(show_calibration, Show soil sensor calibration status);
//MSH_CMD_EXPORT(set_calib, Set soil sensor calibration values);
//MSH_CMD_EXPORT(sensor_status, Show quick status of all sensors);
//MSH_CMD_EXPORT(sgp30_debug, SGP30 debug information);
//MSH_CMD_EXPORT(sgp30_save_baseline, Save SGP30 baseline values);
//MSH_CMD_EXPORT(sgp30_load_baseline, Load SGP30 baseline values);
//
///* 风扇控制命令导出 */
//MSH_CMD_EXPORT(fan_control, Control fan: on/off/auto);
//MSH_CMD_EXPORT(fan_status, Show fan control status);
//MSH_CMD_EXPORT(set_fan_threshold, Set fan temperature thresholds);
//MSH_CMD_EXPORT(set_fan_speed, Set fan speed (0-100%));
//
///* 水泵控制命令导出 */
//MSH_CMD_EXPORT(pump_control, Control pump: on/off/auto);
//MSH_CMD_EXPORT(pump_status, Show pump control status);
//MSH_CMD_EXPORT(set_pump_threshold, Set pump humidity thresholds);
//
///* LED控制命令导出 */
//MSH_CMD_EXPORT(led_control, Control LED: off/on/breath/blink);
//MSH_CMD_EXPORT(led_status, Show LED control status);
//
///* 蜂鸣器控制命令导出 */
//MSH_CMD_EXPORT(buzzer_control, Control buzzer: off/on/beep/alarm);
//MSH_CMD_EXPORT(buzzer_status, Show buzzer control status);
//MSH_CMD_EXPORT(buzzer_beep, Single beep with duration in ms);
//MSH_CMD_EXPORT(buzzer_alarm, Alarm with multiple beeps);
//



//
//int main(void)
//{
//    // 等待系统稳定
//    rt_thread_mdelay(1000);
//
//    // 初始化TJC串口
//    if (tjc_uart_init() != RT_EOK) {
//        rt_kprintf("TJC UART init failed!\n");
//        return -1;
//    }
//
//    // 等待初始化完成
//    rt_thread_mdelay(500);
//
//    // 切换到hw390页面
//    tjc_send_string("page hw390");
//    rt_thread_mdelay(300);
//
//    // 设置t0.txt的内容
//    tjc_send_txt("t0", "txt", "Hello!");
//
//    rt_kprintf("Text set successfully!\n");
//
//    // 保持程序运行
//    while (1) {
//        rt_thread_mdelay(1000);
//    }
//
//    return 0;
//}
