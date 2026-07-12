///*
// * Copyright (c) 2006-2021, RT-Thread Development Team
// *
// * SPDX-License-Identifier: Apache-2.0
// *
// * Change Logs:
// * Date           Author       Notes
// * 2026-03-23     17625       the first version
// */
#ifndef APPLICATIONS_CORE_PROCESS_H_
#define APPLICATIONS_CORE_PROCESS_H_


#include "common.h"

/* 融合线程配置 */
#define THREAD_FUSION_PRIORITY     9
#define THREAD_FUSION_STACK_SIZE   1024
#define THREAD_FUSION_TICK         500

#define DEADZONE 15   // 死区阈值：转速变化超过 15%才更新

/* 时间管理线程配置 */
#define THREAD_TIME_PRIORITY     5
#define THREAD_TIME_STACK_SIZE   512
#define THREAD_TIME_TICK         1000  // 1秒更新间隔

/* ==================== 报警阈值偏移量（超出目标范围后额外增加的值） ==================== */
#define ALARM_TEMP_OFFSET       2.0f   // 空气温度报警偏移 ±2°C
#define ALARM_HUMI_OFFSET       7.0f  // 空气湿度报警偏移 ±7%RH
#define ALARM_SOIL_TEMP_OFFSET  2.0f   // 土壤温度报警偏移 ±2°C
#define ALARM_SOIL_HUMI_OFFSET  7     // 土壤湿度报警偏移 ±7%
#define ALARM_CO2_OFFSET        200    // CO2浓度报警偏移 +200 ppm
#define ALARM_TVOC_OFFSET       100    // TVOC浓度报警偏移 +100 ppb
#define ALARM_LIGHT_OFFSET      500    // 光照报警偏移 +500 lux

/* 目标环境参数结构体（包含空气和土壤） */
typedef struct {
    // 空气环境
    float temp_min;          /* 最低空气温度（℃） */
    float temp_max;          /* 最高空气温度（℃） */
    float humid_min;         /* 最低空气湿度（%RH） */
    float humid_max;         /* 最高空气湿度（%RH） */
    // 土壤环境
    float soil_temp_min;     /* 最低土壤温度（℃） */
    float soil_temp_max;     /* 最高土壤温度（℃） */
    float soil_humid_min;    /* 最低土壤湿度（%） */
    float soil_humid_max;    /* 最高土壤湿度（%） */
    // 光照与CO₂
    int light_min;         /* 最低光照（lux） */
    int light_max;         /* 最高光照（lux） */
    int eco2_min;            /* 最低CO₂（ppm） */
    int eco2_max;            /* 最高CO₂（ppm） */
    int tvoc_min;            /* 最低tvoc（ppd） */
    int tvoc_max;            /* 最高tvoc（ppd） */
} target_env_t;

/* ==================== 蘑菇品种目标环境参数 ==================== */
/* 红菇 —— 高温高湿，典型的热带/亚热带品种，需弱光刺激 */
static const target_env_t target_env_honggu = {
    .temp_min = 26.0f, .temp_max = 30.0f,
    .humid_min = 85.0f, .humid_max = 95.0f,
    .soil_temp_min = 24.0f, .soil_temp_max = 28.0f,
    .soil_humid_min = 75.0f, .soil_humid_max = 85.0f,
    .light_min = 400,  .light_max = 2500,
    .eco2_min = 400, .eco2_max = 1000,
    .tvoc_min = 0,   .tvoc_max = 100,
};

/* 蓝菇 —— 中温高湿，喜光品种，需要较强散射光 */
static const target_env_t target_env_langu = {
    .temp_min = 22.0f, .temp_max = 26.0f,
    .humid_min = 80.0f, .humid_max = 92.0f,
    .soil_temp_min = 20.0f, .soil_temp_max = 24.0f,
    .soil_humid_min = 70.0f, .soil_humid_max = 82.0f,
    .light_min = 1500, .light_max = 3000,
    .eco2_min = 400, .eco2_max = 700,
    .tvoc_min = 0,   .tvoc_max = 100,
};

/* 绿菇 —— 中温中湿，光照适应性最广 */
static const target_env_t target_env_lvgu = {
    .temp_min = 20.0f, .temp_max = 24.0f,
    .humid_min = 75.0f, .humid_max = 88.0f,
    .soil_temp_min = 18.0f, .soil_temp_max = 22.0f,
    .soil_humid_min = 65.0f, .soil_humid_max = 78.0f,
    .light_min = 750, .light_max = 1500,
    .eco2_min = 400, .eco2_max = 800,
    .tvoc_min = 0,   .tvoc_max = 100,
};

/* 云菇 —— 低温低湿，模拟高海拔冷凉环境，极弱光 */
static const target_env_t target_env_yungu = {
    .temp_min = 14.0f, .temp_max = 18.0f,
    .humid_min = 65.0f, .humid_max = 78.0f,
    .soil_temp_min = 12.0f, .soil_temp_max = 16.0f,
    .soil_humid_min = 55.0f, .soil_humid_max = 68.0f,
    .light_min = 150,  .light_max = 300,
    .eco2_min = 400, .eco2_max = 550,
    .tvoc_min = 0,   .tvoc_max = 100,
};

/* 黄菇 —— 中温中高湿，避光品种，对光照敏感 */
static const target_env_t target_env_huanggu = {
    .temp_min = 21.0f, .temp_max = 25.0f,
    .humid_min = 78.0f, .humid_max = 90.0f,
    .soil_temp_min = 19.0f, .soil_temp_max = 23.0f,
    .soil_humid_min = 68.0f, .soil_humid_max = 80.0f,
    .light_min = 50,  .light_max = 350,
    .eco2_min = 400, .eco2_max = 850,
    .tvoc_min = 0,   .tvoc_max = 100,
};

/* 默认目标 —— 包含所有品种的极端值，作为安全兜底 */
static const target_env_t target_env_default = {
    .temp_min = 14.0f, .temp_max = 30.0f,
    .humid_min = 65.0f, .humid_max = 95.0f,
    .soil_temp_min = 12.0f, .soil_temp_max = 28.0f,
    .soil_humid_min = 55.0f, .soil_humid_max = 85.0f,
    .light_min = 1000,   .light_max = 2000,
    .eco2_min = 400, .eco2_max = 1000,
    .tvoc_min = 0,   .tvoc_max = 100,
};


int fusion_thread_init(void);
const target_env_t* get_target_by_mushroom_type(mushroom_type_t type);
void process_threads_stop(void);
int process_threads_init(void);

#endif /* APPLICATIONS_CORE_PROCESS_H_ */
