/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-05     17625       Updated with communication mechanisms
 */
#ifndef APPLICATIONS_COMMON_H_

#define APPLICATIONS_COMMON_H_

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG

extern RTC_HandleTypeDef hrtc;   // 板级支持包中已定义

/* ==================== 系统配置 ==================== */
#define SYS_HEARTBEAT_INTERVAL   1000    // 心跳间隔1秒
#define ALARM_COUNT              6       // 报警数量

/* 风扇状态枚举 */
typedef enum {
    FAN_MANU = 0,       //手动模式
    FAN_AUTO = 1,       //自动模式
} fan_mode_t;

/* 水泵状态枚举 */
typedef enum {
    PUMP_MANU = 0,
    PUMP_AUTO = 1,      //自动模式
} pump_mode_t;

/* LED工作模式枚举 */
typedef enum {
    LED_MANU = 0,       //手动模式
    LED_AUTO = 1,       //自动模式
} led_mode_t;

/* 蜂鸣器状态枚举 */
typedef enum {
    BUZZER_MANU = 0,    //手动模式
    BUZZER_AUTO = 1,    //自动模式
} buzzer_mode_t;

/* 加湿器状态枚举 */
typedef enum {
    ATOMIZ_MANU = 0,    //手动模式
    ATOMIZ_AUTO = 1,    //自动模式
} atomiz_mode_t;

/* 音乐播放器状态枚举 */
typedef enum {
    AUDIO_MANU = 0,    //手动模式
    AUDIO_AUTO = 1,    //自动模式
} audio_mode_t;


/* 命令来源枚举 */
typedef enum {
    LOCAL = 0,    //本地命令
    CLOUD = 1,    //云端命令
    INTER = 2,    //内部命令
} cmd_source_t;

/* 命令类型枚举 */
typedef enum {
    MSG_ALL = 0,
    MSG_FAN = 1,
    MSG_PUMP = 2,
    MSG_LED = 3,
    MSG_BUZZER = 4,
    MSG_ATOMIZ = 5,
    MSG_AUDIO = 6,
} msg_type_t;

/* ==================== 传感器类型枚举 ==================== */
typedef enum {
    SENSOR_SOIL_MOISTURE = 1,    // 土壤湿度
    SENSOR_SOIL_TEMPERATURE = 2, // 土壤温度
    SENSOR_TEMPERATURE = 3,      // 温度
    SENSOR_HUMIDITY = 4,         // 湿度
    SENSOR_LIGHT = 5,            // 光照
    SENSOR_ECO2 = 6,             // eCO2
    SENSOR_TVOC = 7,              // TVOC
    SENSOR_AI = 8,                // AI
    SENSOR_WEATHER = 9,          //天气类型
    SENSOR_TIME = 10,             // 时间

    /* 执行器状态类型 (从 20 开始，避免与传感器冲突) */
    ACTUATOR_FAN = 20,
    ACTUATOR_PUMP = 21,
    ACTUATOR_LED = 22,
    ACTUATOR_BUZZER = 23,
    ACTUATOR_ATOMIZ = 24,
    ACTUATOR_AUDIO = 25,
} sensor_type_t;

/* ==================== 传感器独立结构体 ==================== */
/* 土壤湿度传感器数据 */
typedef struct {
    int raw_value;           // 原始ADC值
    float voltage;           // 电压值
    int humidity_percentage; // 湿度百分比
    const char* status;      // 状态描述
    rt_bool_t ready;         // 就绪标志
    rt_tick_t last_update;   // 最后更新时间
} soil_data_t;

typedef struct {
    float soil_temperature;
    rt_tick_t last_update;
    rt_bool_t ready;
} ds18b20_data_t;

/* BH1750光照传感器数据 */
typedef struct {
    float light_level;              // 光照强度
    rt_bool_t ready;                // 就绪标志
    rt_tick_t last_update;          // 最后更新时间
} bh1750_data_t;

/* SHT3X温湿度传感器数据 */
typedef struct {
    float temperature;              // 温度
    float humidity;                 // 湿度
    rt_bool_t ready;                // 就绪标志
    rt_tick_t last_update;          // 最后更新时间
} sht3x_data_t;

/* SGP30空气质量传感器数据 */
typedef struct {
    rt_uint16_t eco2;               // CO2浓度
    rt_uint16_t tvoc;               // TVOC浓度
    rt_bool_t ready;                // 就绪标志
    rt_bool_t data_valid;           // 数据有效标志
    rt_uint8_t warmup_percent;      // 预热进度
    rt_tick_t last_update;          // 最后更新时间
} sgp30_data_t;

/* ==================== 菌菇类型枚举 ==================== */
typedef enum {
    MUSHROOM_TYPE_UNKNOWN = 0,
    MUSHROOM_TYPE_HONGGU = 1,      // 红菇
    MUSHROOM_TYPE_LANGU = 2,    // 蓝菇
    MUSHROOM_TYPE_LVGU = 3,     // 绿菇
    MUSHROOM_TYPE_YUNGU = 4,    //云菇
    MUSHROOM_TYPE_HUANGGU = 5    //黄菇
} mushroom_type_t;

/* ==================== 视觉识别数据结构 ==================== */
typedef struct {
    mushroom_type_t type;      // 识别出的菌菇种类
    float confidence;          // 置信度 (0.0 ~ 1.0)
    rt_bool_t ready;           // 数据就绪标志
    rt_tick_t last_update;     // 最后更新时间
} vision_data_t;


/* 新增：单日天气详细数据（用于三天预报） */
typedef struct {
    char date[12];              /* 日期 "2026-04-05" */
    char text_day[32];          /* 白天天气现象 */
    char text_night[32];        /* 夜间天气现象 */
    int code_day;               /* 白天天气代码 */
    int code_night;             /* 夜间天气代码 */
    int high;                   /* 最高温度（℃） */
    int low;                    /* 最低温度（℃） */
    char wind_direction[16];    /* 风向 */
    char wind_scale[8];         /* 风力等级 */
} weather_day_t;

/* 三天预报总结构 */
typedef struct {
    weather_day_t days[3];      /* 0=今天，1=明天，2=后天 */
} weather_3day_t;


/* 新增时间数据结构 */
typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t day;               // 可选，备后续使用
    uint8_t month;
    uint16_t year;
} time_data_t;


/* ==================== 传感器消息结构 ==================== */
typedef struct {
    sensor_type_t type;          // 消息类型
    rt_uint32_t timestamp;       // 时间戳

    union {
        /* 土壤传感器数据 */
        struct {
            rt_int16_t  raw_value;
            float voltage;
            float  humidity_percent;
        } soil;

        /* 温湿度数据 */
        struct {
            float temperature;
            float humidity;
        } env;

        /* 空气质量数据 */
        struct {
            rt_uint32_t eco2;
            rt_uint32_t tvoc;
        } air;

        /* 光照数据 */
        float light_lux;

        /* 土壤温度数据 */
        float soil_temperature;

        /* AI识别数据 */
        struct {
            int8_t type;      // 识别出的菌菇种类
            float confidence;          // 置信度 (0.0 ~ 1.0)
        } ai;

        time_data_t time;               // 新增：时间数据

        /* ========== 执行器数据 ========== */
        struct {
            int8_t work_mode;   // 0=手动,1=自动
            rt_bool_t state;     // 开关状态
            int32_t value;       // 转速、亮度等数值（0-100）
        } actor;

    } data;
} sensor_msg_t;
/* ==================== 命令结构体 ==================== */
typedef struct {
    cmd_source_t source;      // 命令来源
    msg_type_t   type;        // 消息类型
    rt_uint32_t  timestamp;   // 时间戳
    // 取值：0=广播，1=风扇，2=水泵，3=LED，4=蜂鸣器，5=加湿器
    union {
        /*风扇命令*/
        struct {
            rt_bool_t fan_status;
            int speed;
            fan_mode_t mode;  // 风扇模式
        } fan;

        /*LED命令*/
        struct {
            rt_bool_t led_status;
            int brightness;
            led_mode_t mode;   // LED模式
        } led;

        /*水泵*/
        struct {
            rt_bool_t pump_status;
            pump_mode_t mode; // 水泵模式
        } pump;

        /*加湿器*/
        struct {
            rt_bool_t atomiz_status;
            atomiz_mode_t mode; // 加湿器模式
        } atomiz;

        /*播放器*/
        struct {
            rt_bool_t audio_status;
            audio_mode_t mode; // 播放器模式
        } audio;

        /*蜂鸣器*/
        struct {
            rt_bool_t buzzer_status;
            buzzer_mode_t mode; // 蜂鸣器模式
        } buzzer;
    } data;

} command_t;

/* 风扇控制数据 */
typedef struct {
    fan_mode_t mode;              // 工作状态
    rt_bool_t current_status;       // 当前运行状态
    int speed;                      // 速度百分比
    rt_tick_t last_update;          // 最后更新时间
} fan_data_t;

/* 水泵控制数据 */
typedef struct {
    pump_mode_t mode;             // 工作状态
    rt_bool_t current_status;       // 当前运行状态
    rt_tick_t last_update;          // 最后更新时间
} pump_data_t;

/* 加湿器控制数据 */
typedef struct {
    atomiz_mode_t mode;              // 工作模式
    rt_bool_t current_status;        // 当前运行状态
    rt_tick_t last_update;           // 最后更新时间
} atomiz_data_t;

/* LED控制数据 */
typedef struct {
    led_mode_t mode;                // 工作模式
    int brightness;                 // 亮度百分比
    rt_bool_t current_status;       // 当前运行状态
    rt_tick_t last_update;          // 最后更新时间
} led_data_t;

/* 播放器控制数据 */
typedef struct {
    audio_mode_t mode;                // 工作模式
    rt_bool_t current_status;       // 当前运行状态
    rt_tick_t last_update;          // 最后更新时间
} audio_data_t;

/* 蜂鸣器控制数据 */
typedef struct {
    buzzer_mode_t mode;             // 工作状态
    rt_bool_t current_status;       // 当前状态
    rt_tick_t last_update;          // 最后更新时间
} buzzer_data_t;


/* 系统状态数据 */
typedef struct {
    rt_uint32_t uptime;              // 系统运行时间（秒）
    rt_uint8_t system_status;        // 系统状态
    rt_uint8_t alarm_status[6];      // 报警状态，6个报警状态
} system_data_t;

/* 总的数据结构 */
typedef struct {
    soil_data_t soil;
    ds18b20_data_t ds18b20;
    bh1750_data_t bh1750;
    sgp30_data_t sgp30;
    sht3x_data_t sht3x;
    fan_data_t fan;
    pump_data_t pump;
    led_data_t led;
    buzzer_data_t buzzer;
    atomiz_data_t atomiz;
    audio_data_t audio;
    vision_data_t vision;      // 视觉识别数据
    weather_3day_t weather;     //天气数据
    time_data_t   time;
    system_data_t system;           // 系统状态数据
} sensor_data_t;

/* ==================== 执行器专用队列声明 ==================== */
/* 全局消息队列声明 */
extern rt_mq_t display_queue;
extern rt_mq_t upload_queue;
extern rt_mq_t fusion_queue;
extern rt_mq_t fan_queue;
extern rt_mq_t pump_queue;
extern rt_mq_t led_queue;
extern rt_mq_t buzzer_queue;
extern rt_mq_t atomiz_queue;
extern rt_mq_t audio_queue;

/* 全局通信对象 */
extern sensor_data_t sensor_data;
extern rt_mutex_t data_mutex;
extern rt_event_t system_events;

/* 全局天气数据（外部定义） */
extern weather_3day_t g_weather_data;
extern rt_mutex_t g_weather_mutex;

/* 全局邮箱声明 */
extern rt_mailbox_t atomiz_mailbox;
extern rt_mailbox_t fan_mailbox;
extern rt_mailbox_t pump_mailbox;
extern rt_mailbox_t led_mailbox;
extern rt_mailbox_t buzzer_mailbox;
extern rt_mailbox_t audio_mailbox;

/* 全局函数声明 */
void post_event(rt_uint32_t events);
rt_bool_t check_event(rt_uint32_t events);

void lock_data(void);
void unlock_data(void);

#endif /* APPLICATIONS_COMMON_H_ */
