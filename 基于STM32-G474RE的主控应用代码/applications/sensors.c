/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-02     17625       the first version
 */
#include "sensors.h"
#include "common.h"
#include <adc.h>
#include <rtdbg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ipc_manager.h"
/* 传感器头文件 */
#include "bh1750.h"
#include "sgp30.h"
#include "sht3x.h"
#include "dallas_ds18b20_sensor_v1.h"
#include "stm32g4xx_hal.h"

/* 请根据实际硬件修改数据引脚 */
#define DS18B20_DATA_PIN    GET_PIN(B, 13)

//滑动平均滤波参数：平滑 ADC 采样值，抑制瞬时噪声
#define SOIL_FIFO_SIZE 5
static rt_uint32_t soil_fifo[SOIL_FIFO_SIZE];
static int soil_fifo_index = 0;

//SHT30平滑系数（新旧数据的权重）
#define SHT3X_ALPHA 0.5f   // 滤波系数，越小越平滑

/* 土壤传感器配置数组 */
static soil_sensor_t soil_sensor = {
    .adc_name = "adc1",
    .channel = 2,
    .air_value = 2900,
    .water_value = 850,
    .interval = 0,
    .name = "Soil"
};

/* 环境传感器设备指针 */
static bh1750_device_t light_sensor = RT_NULL;
static sgp30_device_t sgp30_dev = RT_NULL;
static sht3x_device_t sht3x_dev = RT_NULL;
static rt_device_t ds18b20_dev = RT_NULL;
static rt_adc_device_t soil_adc_dev;

/* SGP30 状态标志 */
static rt_bool_t sgp30_initialized = RT_FALSE;
static rt_tick_t sgp30_start_time = 0;

/* 传感器线程控制标志 */
static rt_bool_t sensors_running = RT_FALSE;
static rt_thread_t soil_thread = RT_NULL;
static rt_thread_t sht3x_thread = RT_NULL;
static rt_thread_t bh1750_thread = RT_NULL;
static rt_thread_t sgp30_thread = RT_NULL;
static rt_thread_t ds18b20_thread = RT_NULL;

//AIuart线程全局变量
static rt_thread_t ai_thread = RT_NULL;
static rt_device_t uart_dev = RT_NULL;
static rt_sem_t rx_sem = RT_NULL;          // 用于通知有数据到达
static char line_buf[256];                  // 行缓冲区，用于拼接不完整的行
static int line_pos = 0;                    // 当前行缓冲区已使用长度
/* ==================== 辅助函数 ==================== */
static int soil_sensor_init(void);
static int ds18b20_sensor_init(void);
static rt_uint32_t read_soil_raw_value(void);
static const char* get_soil_moisture_status(int soil_value);
static int calculate_soil_humidity_percentage(int soil_value);
static rt_bool_t sgp30_check_warmup_status(void);
static rt_bool_t sgp30_apply_humidity_compensation(void);
static rt_err_t sgp30_initialize_with_retry(void);
static void parse_ai_message(const char *buffer);
static rt_err_t uart_rx_ind(rt_device_t dev, rt_size_t size);

/* ==================== 土壤湿度传感器实现 ==================== */
static int soil_sensor_init(void)
{
    soil_adc_dev = (rt_adc_device_t)rt_device_find(soil_sensor.adc_name);

    if (soil_adc_dev == RT_NULL)
    {
        LOG_E("ADC device %s for %s not found!", soil_sensor.adc_name, soil_sensor.name);
        return -RT_ERROR;
    }

    if (rt_adc_enable(soil_adc_dev, soil_sensor.channel) != RT_EOK)
    {
        LOG_E("Failed to enable ADC channel %d for %s", soil_sensor.channel, soil_sensor.name);
        return -RT_ERROR;
    }

    soil_sensor.interval = (soil_sensor.air_value - soil_sensor.water_value) / 3;

    lock_data();
    sensor_data.soil.ready = RT_TRUE;
    unlock_data();

    LOG_I("%s initialized. Channel: %d", soil_sensor.name, soil_sensor.channel);
    return RT_EOK;
}

/* 滑动平均滤波 */
static rt_uint32_t soil_filtered_read(void)
{
    rt_uint32_t sum = 0;
    for (int i = 0; i < SOIL_FIFO_SIZE; i++) {
        sum += soil_fifo[i];
    }
    return sum / SOIL_FIFO_SIZE;
}

/* 读取并滤波 */
static rt_uint32_t read_soil_raw_value(void)
{
    rt_uint32_t raw = rt_adc_read(soil_adc_dev, soil_sensor.channel);
    soil_fifo[soil_fifo_index] = raw;
    soil_fifo_index = (soil_fifo_index + 1) % SOIL_FIFO_SIZE;
    return soil_filtered_read();
}

/* 异常检测 */
static rt_bool_t soil_value_valid(rt_uint32_t raw)
{
    if (raw < 100 || raw > 4000) return RT_FALSE;  // 超出典型范围
    return RT_TRUE;
}

static const char* get_soil_moisture_status(int soil_value)
{
    if (!sensor_data.soil.ready)
        return "Invalid Sensor";

    int air_value = soil_sensor.air_value;
    int water_value = soil_sensor.water_value;
    int interval = soil_sensor.interval;

    if (soil_value > water_value && soil_value <= (water_value + interval))
        return "Very Wet";
    else if (soil_value > (water_value + interval) && soil_value <= (air_value - interval))
        return "Wet";
    else if (soil_value < air_value && soil_value >= (air_value - interval))
        return "Dry";
    else if (soil_value >= air_value)
        return "Very Dry";
    else if (soil_value <= water_value)
        return "Over Wet";
    else
        return "Unknown";
}

static int calculate_soil_humidity_percentage(int soil_value)
{
    if (!sensor_data.soil.ready)
        return -1;

    int air_value = soil_sensor.air_value;
    int water_value = soil_sensor.water_value;
    int moisture_percentage;

    if (soil_value >= air_value)
        moisture_percentage = 0;
    else if (soil_value <= water_value)
        moisture_percentage = 100;
    else
        moisture_percentage = 100 - ((soil_value - water_value) * 100) / (air_value - water_value);

    if (moisture_percentage < 0) moisture_percentage = 0;
    if (moisture_percentage > 100) moisture_percentage = 100;

    return moisture_percentage;
}
/* ==================== 土壤温度传感器初始化 ==================== */
static int ds18b20_sensor_init(void)
{

    struct rt_sensor_config cfg;
    cfg.intf.user_data = (void *)DS18B20_DATA_PIN;
    rt_hw_ds18b20_init("ds18b20", &cfg);

    rt_thread_mdelay(100);
    ds18b20_dev = rt_device_find("temp_ds18b20");   // 注意前缀
    /* 查找 DS18B20 设备（设备名称由驱动注册时确定，一般为 "temp_ds18b20"） */
    if (ds18b20_dev == RT_NULL)
    {
        LOG_E("DS18B20 device not found!");
        return -RT_ERROR;
    }

    /* 打开设备 */
    if (rt_device_open(ds18b20_dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("Failed to open DS18B20 device");
        return -RT_ERROR;
    }

    /* 设置输出数据率（可选，根据驱动支持情况） */
    rt_device_control(ds18b20_dev, RT_SENSOR_CTRL_SET_ODR, (void *)100); // 100ms

    /* 标记传感器就绪 */
    lock_data();
    sensor_data.ds18b20.ready = RT_TRUE;
    unlock_data();

    LOG_I("DS18B20 initialized on pin %d", DS18B20_DATA_PIN);
    return RT_EOK;
}

/* ==================== 环境传感器初始化 ==================== */
rt_err_t env_sensors_init(void)
{
    LOG_I("Starting environment sensors initialization...");

    if (soil_sensor_init() != RT_EOK)
    {
        LOG_W("Soil sensor initialization failed");
    }

    if (ai_uart_init() != RT_EOK)
    {
        LOG_W("AI UART initialization failed");
    }

    /* 初始化 DS18B20 传感器 */
    if (ds18b20_sensor_init() != RT_EOK)
    {
        LOG_W("DS18B20 initialization failed, thread not created");
    }

    /* 初始化 BH1750 */
    light_sensor = bh1750_init("i2c3");
    if (light_sensor != RT_NULL)
    {
        lock_data();
        sensor_data.bh1750.ready = RT_TRUE;
        unlock_data();
        LOG_I("BH1750 initialize success on i2c3!");
    }
    else
    {
        LOG_E("BH1750 initialize failed on i2c3!");
    }

    /* 初始化 SHT3X */
    sht3x_dev = sht3x_init("i2c2", 0x44);
    if (sht3x_dev != RT_NULL)
    {
        lock_data();
        sensor_data.sht3x.ready = RT_TRUE;
        unlock_data();
        LOG_I("SHT3X initialize success on i2c2!");
    }
    else
    {
        LOG_E("SHT3X initialize failed on i2c2!");
    }

    /* 初始化 SGP30 */
    if (sgp30_initialize_with_retry() == RT_EOK)
    {
        lock_data();
        sensor_data.sgp30.ready = RT_TRUE;
        sensor_data.sgp30.data_valid = RT_FALSE;
        sensor_data.sgp30.warmup_percent = 0;
        unlock_data();
        LOG_I("SGP30 initialize success on i2c1!");
    }
    else
    {
        LOG_E("SGP30 initialize failed on i2c1!");
    }

    rt_thread_mdelay(2000);

    if (sensor_data.bh1750.ready || sensor_data.sht3x.ready
            || sensor_data.sgp30.ready || sensor_data.soil.ready
            || sensor_data.ds18b20.ready|| sensor_data.vision.ready)
    {
        LOG_I("Environment sensors initialization completed");
        return RT_EOK;
    }
    else
    {
        LOG_E("All environment sensors initialization failed!");
        return -RT_ERROR;
    }
}
/* ====================AI 串口初始化 ==================== */
rt_err_t ai_uart_init(void)
{
    uart_dev = rt_device_find(AI_UART_DEVICE);
    if (uart_dev == RT_NULL) {
        LOG_E("UART device %s not found", AI_UART_DEVICE);
        return -RT_ERROR;
    }

    /* 设置串口参数（与之前相同） */
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = AI_UART_BAUD;
    config.data_bits = AI_UART_DATA_BITS;
    config.stop_bits = AI_UART_STOP_BITS;
    config.parity    = AI_UART_PARITY;
    config.bufsz     = RX_BUFFER_SIZE;   // 建议已增大到 1024+
    rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &config);

    /* 打开设备，使用中断接收模式 */
    rt_device_open(uart_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);

    /* 创建信号量 */
    rx_sem = rt_sem_create("ai_rx", 0, RT_IPC_FLAG_FIFO);
    if (rx_sem == RT_NULL) {
        LOG_E("Create AI rx semaphore failed");
        return -RT_ENOMEM;
    }

    /* 设置接收指示回调 */
    rt_device_set_rx_indicate(uart_dev, uart_rx_ind);

    return RT_EOK;
}
//AI 串口回调函数
static rt_err_t uart_rx_ind(rt_device_t dev, rt_size_t size)
{
    /* 释放信号量，通知接收线程有数据可读 */
    rt_sem_release(rx_sem);
    return RT_EOK;
}

/* ==================== SGP30 优化函数 ==================== */

/* 2. 保存基线数据到Flash */
void sgp30_save_baseline(uint16_t eco2_base, uint16_t tvoc_base)
{
    static uint16_t last_saved_eco2 = 0xFFFF;
    static uint16_t last_saved_tvoc = 0xFFFF;
    // 如果与上次保存的值相同，则跳过
    if (eco2_base == last_saved_eco2 && tvoc_base == last_saved_tvoc) {
        return;
    }
    /* 注意：必须以64位(双字)为单位写入 */
    uint64_t data = ((uint64_t)tvoc_base << 32) | (uint64_t)eco2_base;
    uint32_t addr = SGP30_BASELINE_FLASH_ADDR;

    rt_base_t level = rt_hw_interrupt_disable();  // 关闭全局中断

    HAL_FLASH_Unlock();
    /* 可选: 清除可能存在的错误标志 */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    // 双 BANK 模式下：BANK2 起始地址 0x08040000，每页 2KB
    const uint32_t bank2_start = 0x08040000;
    const uint32_t page_size = 2048;
    uint32_t page = (addr - bank2_start) / page_size;   // 页号从 0 开始
    uint32_t bank = FLASH_BANK_2;

    LOG_I("Erasing BANK2 page %d at 0x%X", page, addr);
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Banks = bank,
        .Page = page,
        .NbPages = 1
    };
    uint32_t page_err;
    if (HAL_FLASHEx_Erase(&erase, &page_err) != HAL_OK) {
        LOG_E("Flash erase failed, page=%lu, error page=%lu", page, page_err);
        goto exit;
    }
    // 重试写入
    int retry = 3;
    while (retry--) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data) == HAL_OK) {
            break;
        }
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
        rt_thread_mdelay(1);
    }
    if (retry < 0) {
        LOG_E("Flash program failed after retries");
    } else {
        LOG_I("SGP30 baseline saved: eCO2=0x%04X, TVOC=0x%04X @ 0x%X (BANK2 page %d)",eco2_base, tvoc_base, addr, page);
        last_saved_eco2 = eco2_base;
        last_saved_tvoc = tvoc_base;
    }

exit:
    HAL_FLASH_Lock();
    rt_hw_interrupt_enable(level);
}

/* 3. 加载基线数据 */
rt_bool_t sgp30_load_baseline(uint16_t *eco2_base, uint16_t *tvoc_base)
{
    uint64_t *addr = (uint64_t*)SGP30_BASELINE_FLASH_ADDR;
    uint64_t data = *addr;

    if (data == 0xFFFFFFFFFFFFFFFF) {
        LOG_I("No valid baseline found in Flash");
        return RT_FALSE;
    }

    *eco2_base = (uint16_t)data;
    *tvoc_base = (uint16_t)(data >> 32);
    LOG_I("SGP30 baseline loaded: eCO2=0x%04X, TVOC=0x%04X", *eco2_base, *tvoc_base);
    return RT_TRUE;
}
static rt_bool_t sgp30_check_warmup_status(void)
{
    if (!sgp30_initialized) return RT_FALSE;

    rt_tick_t current_time = rt_tick_get();
    rt_tick_t elapsed_time = current_time - sgp30_start_time;
    const rt_tick_t warmup_time = 20000;

    lock_data();
    if (elapsed_time >= warmup_time)
    {
        sensor_data.sgp30.warmup_percent = 100;
        unlock_data();
        return RT_TRUE;
    }
    else
    {
        sensor_data.sgp30.warmup_percent = (elapsed_time * 100) / warmup_time;
        unlock_data();
        return RT_FALSE;
    }
}

static rt_bool_t sgp30_apply_humidity_compensation(void)
{
    // 增加状态检查：SGP30 已初始化且数据已稳定
    if (!sgp30_initialized || !sensor_data.sgp30.data_valid) {
        return RT_FALSE;
    }
    // 添加静态计数器，用于降低补偿频率
    static uint8_t humidity_comp_counter = 0;

    // 降低调用频率
    humidity_comp_counter++;
    if (humidity_comp_counter % HUMIDITY_COMP_INTERVAL != 0) {
        return RT_TRUE;   // 跳过本次补偿，但返回成功（避免上层误判）
    }

    if (sensor_data.sht3x.ready && sgp30_dev != RT_NULL)
    {
        lock_data();
        float temperature = sensor_data.sht3x.temperature;
        float humidity = sensor_data.sht3x.humidity;
        unlock_data();

        if (temperature > -20.0f && temperature < 60.0f && humidity >= 0.0f && humidity <= 100.0f)
        {
            // 使用 Magnus 公式计算饱和蒸气压（单位：hPa）
            float es = 6.112f * expf((17.67f * temperature) / (temperature + 243.5f));

            // 计算绝对湿度（g/m³）
            float absolute_humidity = (es * humidity * 2.1674f) / (273.15f + temperature);

            rt_uint32_t ah_scaled = (rt_uint32_t)(absolute_humidity * 1000.0f);  // 转为 mg/m³

            if (sgp30_set_humidity(sgp30_dev, ah_scaled))
            {
                return RT_TRUE;
            }
            else
            {
                LOG_W("SGP30 set humidity failed");
            }
        }
    }
    return RT_FALSE;
}

static rt_err_t sgp30_initialize_with_retry(void)
{
    int retry_count = 0;
    const int max_retries = 5;

    LOG_I("Initializing SGP30...");

    while (retry_count < max_retries)
    {
        sgp30_dev = sgp30_create("i2c1");
        if (sgp30_dev == RT_NULL)
        {
            LOG_E("SGP30 create failed (attempt %d/%d)", retry_count + 1, max_retries);
            retry_count++;
            rt_thread_mdelay(1000);
            continue;
        }

//        /* 尝试加载之前保存的基线 */
//        uint16_t eco2_base, tvoc_base;
//        if (sgp30_load_baseline(&eco2_base, &tvoc_base))
//        {
//            if (eco2_base != 0 || tvoc_base != 0) {
//                sgp30_set_baseline(sgp30_dev, eco2_base, tvoc_base);
//                LOG_I("SGP30 baseline restored");
//            } else {
//                LOG_W("Loaded zero baseline, using default");
//            }
//        }

        sgp30_start_time = rt_tick_get();

        if (sgp30_measure(sgp30_dev) == RT_TRUE)
        {
            LOG_I("SGP30 initialized successfully");
            sgp30_initialized = RT_TRUE;
            sgp30_apply_humidity_compensation();
            return RT_EOK;
        }
        else
        {
            LOG_E("SGP30 measurement test failed (attempt %d/%d)", retry_count + 1, max_retries);
            sgp30_delete(sgp30_dev);
            sgp30_dev = RT_NULL;
            retry_count++;
            rt_thread_mdelay(1000);
        }
    }

    LOG_E("SGP30 initialization failed after %d attempts", max_retries);
    return -RT_ERROR;
}

/* ==================== 传感器读取函数 ==================== */
void read_soil_sensor(void)
{
    // 检查传感器是否就绪
    if (!sensor_data.soil.ready)
        return;

    rt_thread_mdelay(700);

    // 读取土壤传感器原始值
    rt_uint32_t raw_value = read_soil_raw_value();

    if (!soil_value_valid(raw_value)) {
        //sensor_data.soil.ready = RT_FALSE;
        LOG_W("Soil sensor value out of range: %d", raw_value);
        return;
    }

    float voltage = (raw_value * 3.3f) / 4095.0f;
    const char* status = get_soil_moisture_status(raw_value);
    int humidity = calculate_soil_humidity_percentage(raw_value);

    // 更新传感器数据
    lock_data();
    sensor_data.soil.raw_value = raw_value;
    sensor_data.soil.voltage = voltage;
    sensor_data.soil.status = status;
    sensor_data.soil.humidity_percentage = humidity;
    sensor_data.soil.last_update = rt_tick_get();
    unlock_data();

    /* 发送传感器数据消息 */
    sensor_msg_t msg = {
        .type = SENSOR_SOIL_MOISTURE ,
        .timestamp = rt_tick_get(),
        .data.soil = {
            .raw_value = raw_value,
            .voltage = voltage,
            .humidity_percent = humidity
        }
    };

    // 发送前打印
    //LOG_D("[SOIL] Raw=%d, Voltage=%.2f, Humidity=%d%%", raw_value, voltage, humidity);

    // 1. 发送到显示队列 - 用户需要实时看到数据
    static int soil_upload_counters = 0;
    if (soil_upload_counters++ % 3 == 0) {
        send_to_mq(display_queue,&msg);
        send_to_mq(fusion_queue,&msg);
    }

    // 2. 发送到上传队列 - 云端需要完整记录
    // 可以降低上传频率，比如每3次发送1次
    static int soil_upload_counter = 0;
    if (soil_upload_counter++ % 7 == 0) {
        //LOG_D("[SOIL] Raw=%d, Voltage=%.2f, Humidity=%d%%", raw_value, voltage, humidity);
        if (send_to_mq(upload_queue, &msg) != RT_EOK) {
            LOG_E("Failed to send soil_humidity_percentage to upload queue");
        }
    }
}

void read_ds18b20_sensor(void)
{
    /* 检查传感器是否就绪 */
    if (ds18b20_dev == RT_NULL || !sensor_data.ds18b20.ready)
        return;

    struct rt_sensor_data sensor_ds;
    rt_size_t res = rt_device_read(ds18b20_dev, 0, &sensor_ds, 1);
    if (res != 1)
    {
        LOG_W("DS18B20 read failed, size=%d", res);
        return;
    }

    /* 温度值单位 0.1℃，转换为浮点数 */
    float temperature = sensor_ds.data.temp / 10.0f;

    /* 限幅检查（DS18B20 测量范围 -55°C ~ +125°C） */
    if (temperature < -55.0f || temperature > 125.0f)
    {
        LOG_W("DS18B20 abnormal data: %.1f°C", temperature);
        return;
    }

    /* 更新共享数据 */
    lock_data();
    sensor_data.ds18b20.soil_temperature = temperature;
    sensor_data.ds18b20.last_update = rt_tick_get();
    unlock_data();

    /* 构建温度消息（复用 SENSOR_TEMPERATURE 类型，上层可区分来源） */
    sensor_msg_t msg = {
        .type = SENSOR_SOIL_TEMPERATURE,
        .timestamp = rt_tick_get(),
        .data.soil_temperature = temperature
    };

    //LOG_D("[DS18B20] Temperature=%.1f°C", temperature);

    /* 发送到显示和融合队列（必须） */
    send_to_mq(display_queue, &msg);
    send_to_mq(fusion_queue, &msg);

    /* 上传频率控制（每5次上传1次，模仿土壤传感器的上传策略） */
    static int upload_counter = 0;
    if (upload_counter++ % 5 == 0)
    {
        if (send_to_mq(upload_queue, &msg) != RT_EOK) {
            LOG_E("Failed to send soil_temperature to upload queue");
        }
        //LOG_D("[DS18B20] Temperature=%.1f°C", temperature);
    }

}

void read_sht3x_sensor(void)
{
    if (sht3x_dev == RT_NULL || !sensor_data.sht3x.ready)
        return;

    rt_thread_mdelay(500);

    if (sht3x_read_singleshot(sht3x_dev) == RT_EOK)
    {
        float temp = sht3x_dev->temperature;
        float hum = sht3x_dev->humidity;

        /* 限幅检查 */
        if (temp < -40.0f || temp > 125.0f || hum < 0.0f || hum > 100.0f) {
            LOG_W("SHT3X abnormal data: T=%.2f, H=%.2f", temp, hum);
            return;
        }

        lock_data();
        /* 指数移动平均 */
        if (sensor_data.sht3x.last_update != 0) {
            temp = SHT3X_ALPHA * temp + (1 - SHT3X_ALPHA) * sensor_data.sht3x.temperature;
            hum  = SHT3X_ALPHA * hum  + (1 - SHT3X_ALPHA) * sensor_data.sht3x.humidity;
        }

        sensor_data.sht3x.temperature = temp;
        sensor_data.sht3x.humidity = hum;
        sensor_data.sht3x.last_update = rt_tick_get();
        unlock_data();

        /* 发送温湿度数据 */
        // 发送温度消息
        sensor_msg_t temp_msg = {
            .type = SENSOR_TEMPERATURE,
            .timestamp = rt_tick_get(),
            .data.env.temperature = temp
        };

        // 温度
       // LOG_D("[SHT3X] Temperature=%.2f°C", temp);

        /* 发送温度数据到三个队列 */
        send_to_mq(display_queue,&temp_msg);
        send_to_mq(fusion_queue,&temp_msg);

        static int temp_upload_counter = 0;
        if (temp_upload_counter++ % 5 == 0) {  // 每2次上传1次
            if (send_to_mq(upload_queue, &temp_msg) != RT_EOK) {
                //LOG_E("Failed to send temperature to upload queue");
            }
           // LOG_D("[SHT3X] Temperature=%.2f°C", temp);
        }


        // 发送湿度消息
        sensor_msg_t humid_msg = {
            .type = SENSOR_HUMIDITY,
            .timestamp = rt_tick_get(),
            .data.env.humidity = hum
        };

        // 湿度
        //LOG_D("[SHT3X] Humidity=%.2f%%", hum);
        /* 发送湿度数据到三个队列 */
        send_to_mq(display_queue,&humid_msg);
        send_to_mq(fusion_queue,&humid_msg);

        static int humid_upload_counter = 0;
        if (humid_upload_counter++ % 5 == 0) {  // 每2次上传1次
            if (send_to_mq(upload_queue, &humid_msg) != RT_EOK) {
                LOG_E("Failed to send humid to upload queue");
            }
            //LOG_D("[SHT3X] Humidity=%.2f%%", hum);
        }
    }
}

void read_bh1750_sensor(void)
{
    if (light_sensor == RT_NULL || !sensor_data.bh1750.ready) {
        LOG_E("BH1750 device or function pointer invalid");
        return;
    }

    float light = bh1750_read_light(light_sensor);

    lock_data();
    sensor_data.bh1750.light_level = light;
    sensor_data.bh1750.last_update = rt_tick_get();
    unlock_data();

    // 创建并发送消息
    sensor_msg_t msg = {
        .type = SENSOR_LIGHT,
        .timestamp = rt_tick_get(),
        .data.light_lux = light
    };

    // 1. 发送到显示队列（必须）
    send_to_mq(display_queue,&msg);
    send_to_mq(fusion_queue,&msg);

    // 2. 发送到上传队列（可降低频率，光照变化慢）
    static int light_upload_counter = 0;
    if (light_upload_counter++ % 5 == 0) {  // 每5次上传1次
        if (send_to_mq(upload_queue, &msg) != RT_EOK) {
            LOG_E("Failed to send Light to upload queue");
        }
    }

}

void read_sgp30_sensor(void)
{
    if (sgp30_dev == RT_NULL || !sensor_data.sgp30.ready || !sgp30_initialized)
        return;

    static int measure_count = 0;       // 用于基线保存计数
    static int last_save_count = 0;     // 上次保存时的计数
    //添加静态变量（用于稳定计数）
    static int stable_count = 0;
    static rt_bool_t data_stable = RT_FALSE;

    rt_bool_t is_warmed_up = sgp30_check_warmup_status();


    sgp30_apply_humidity_compensation();

    if (sgp30_measure(sgp30_dev) == RT_TRUE)
    {
        lock_data();
        sensor_data.sgp30.eco2 = sgp30_dev->eCO2;
        sensor_data.sgp30.tvoc = sgp30_dev->TVOC;
        sensor_data.sgp30.last_update = rt_tick_get();

        // 1. 先检查数值是否在合理范围内
        rt_bool_t valid_range = RT_FALSE;
        if (sensor_data.sgp30.eco2 > 400 && sensor_data.sgp30.eco2 < 2000 &&
            sensor_data.sgp30.tvoc > 0 && sensor_data.sgp30.tvoc < 1000) {
            valid_range = RT_TRUE;
        }

        // 2. 预热完成后，还需连续多次满足合理范围才认为数据稳定
        if (is_warmed_up && valid_range) {
            if (!data_stable) {
                stable_count++;
                if (stable_count >= 5) {   // 连续5次测量有效
                    data_stable = RT_TRUE;
                }
            }
            sensor_data.sgp30.data_valid = data_stable;
        } else {
            // 未预热完成或数值不合理，重置稳定计数
            stable_count = 0;
            data_stable = RT_FALSE;
            sensor_data.sgp30.data_valid = RT_FALSE;
        }
        unlock_data();

        /* 仅当数据有效时才发送消息 */
        if (sensor_data.sgp30.data_valid)
        {
            /* 发送空气质量数据 */
            sensor_msg_t eco2_msg = {
                .type = SENSOR_ECO2,
                .timestamp = rt_tick_get(),
                .data.air.eco2 = sgp30_dev->eCO2
            };

            /* eCO2数据分发 */
            send_to_mq(display_queue,&eco2_msg);
            send_to_mq(fusion_queue,&eco2_msg);
            static int eco2_upload_counter = 0;
            if (eco2_upload_counter++ % 5 == 0) {  // 每3次上传1次
                if (send_to_mq(upload_queue, &eco2_msg) != RT_EOK) {
                    LOG_E("Failed to send eco2 to upload queue");
                }
            }

            // 发送TVOC消息
            sensor_msg_t tvoc_msg = {
                .type = SENSOR_TVOC,
                .timestamp = rt_tick_get(),
                .data.air.tvoc = sgp30_dev->TVOC
            };
            /* TVOC数据分发 */
            send_to_mq(display_queue,&tvoc_msg);
            send_to_mq(fusion_queue,&tvoc_msg);

            static int tvoc_upload_counter = 0;
            if (tvoc_upload_counter++ % 5 == 0) {  // 每3次上传1次
                if (send_to_mq(upload_queue, &tvoc_msg) != RT_EOK) {
                    LOG_E("Failed to send tvoc to upload queue");
                }
            }
        }

        /* 定期保存基线（每4000次测量保存一次） */
        measure_count++;
        if (sensor_data.sgp30.data_valid && (measure_count - last_save_count >= 40000)) {
            uint16_t eco2_base, tvoc_base;
            if (sgp30_get_baseline(sgp30_dev, &eco2_base, &tvoc_base)) {
                // 确保基线不是全 0（全 0 表示无效）
                if (eco2_base != 0 || tvoc_base != 0) {
                    sgp30_save_baseline(eco2_base, tvoc_base);
                    last_save_count = measure_count;
                } else {
                    LOG_W("SGP30 baseline is zero, skip saving");
                }
            }
        }
    }
}

// 种类映射函数
static mushroom_type_t str_to_mushroom_type(const char *str)
{
    struct {
        const char *name;
        mushroom_type_t type;
    } map[] = {
        {"HongGu", MUSHROOM_TYPE_HONGGU},
        {"LanGu", MUSHROOM_TYPE_LANGU},
        {"LvGu", MUSHROOM_TYPE_LVGU},
        {"YunGu", MUSHROOM_TYPE_YUNGU},
        {"HuangGu", MUSHROOM_TYPE_HUANGGU},
    };
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (strcmp(str, map[i].name) == 0) {
            return map[i].type;
        }
    }
    return MUSHROOM_TYPE_UNKNOWN;
}

/* ==================== AI 识别函数实现 ==================== */
static void parse_ai_message(const char *buffer)
{
    // 静态变量用于跟踪是否在 BEGIN/END 块内及累积置信度
    static int inside_block = 0;
    static mushroom_type_t current_type = MUSHROOM_TYPE_UNKNOWN;
    static int vote_counts[MUSHROOM_TYPE_HUANGGU + 1] = {0};   // 各类别投票计数
    static int vote_count = 0;                                 // 已投票总次数

    /* 检查是否为 BEGIN 行 */
    if (strstr(buffer, "BEGIN") != NULL)
    {
        //LOG_I("BEGIN detected");
        inside_block = 1;
        current_type = MUSHROOM_TYPE_UNKNOWN;
//        current_confidence = 0.0f;
        return;
    }

    /* 如果在块内 */
    if (inside_block)
    {
        /* 检查是否为 END 行 */
        if (strstr(buffer, "END") != NULL)
        {
            inside_block = 0;

            /* 当前块内识别有效，计入投票 */

            vote_counts[current_type]++;
            vote_count++;
            /* 达到规定次数，执行投票决策 */
            if (vote_count >= AI_VOTE_COUNT) {
                /* 找出得票最多的种类 */
                mushroom_type_t final_type = MUSHROOM_TYPE_UNKNOWN;
                int max_count = 0;
                for (int i = 0; i <= MUSHROOM_TYPE_HUANGGU; i++) {
                    if (vote_counts[i] > max_count) {
                        max_count = vote_counts[i];
                        final_type = (mushroom_type_t)i;
                    }
                }

                float confidence = (float)max_count / AI_VOTE_COUNT;  // 得票比例作为置信度

                /* 更新共享数据 */
                lock_data();
                sensor_data.vision.type = final_type;
                sensor_data.vision.confidence = confidence;
                sensor_data.vision.ready = RT_TRUE;
                sensor_data.vision.last_update = rt_tick_get();
                unlock_data();

                // 创建并发送消息到消息队列
                sensor_msg_t msg = {
                    .type = SENSOR_AI,
                    .timestamp = rt_tick_get(),
                    .data.ai.type = (int8_t)final_type,
                    .data.ai.confidence = confidence
                };

                static int ai_upload_counter = 0;
                if (ai_upload_counter++ % 2 == 0) {  // 每3次上传1次
                    if (send_to_mq(upload_queue, &msg) != RT_EOK) {
                        LOG_E("Failed to send ai to upload queue");
                    }
                        LOG_D("AI recognition: type=%d, confidence=%.2f", final_type, confidence);
                    }

                send_to_mq(display_queue,&msg);
                send_to_mq(fusion_queue,&msg);

                /* 重置投票状态 */
                vote_count = 0;
                memset(vote_counts, 0, sizeof(vote_counts));

            }
            current_type = MUSHROOM_TYPE_UNKNOWN;
            return;
        }
        else
        {
            /* 解析 "类名:置信度" 行，忽略置信度数值 */
            char name[32] = {0};
            float conf;
            if (sscanf(buffer, "%31[^:]:%f", name, &conf) == 2) {
                current_type = str_to_mushroom_type(name);
                if (current_type == MUSHROOM_TYPE_UNKNOWN) {
                    LOG_W("Unknown mushroom name: %s", name);
                }
            }
            else
            {
                LOG_W("Invalid line in AI block: %s", buffer);
            }

        }
    }
    else
    {
        // 不在块内，忽略非 BEGIN 的行（可调试输出）
        LOG_D("Ignored line outside block: %s", buffer);
    }
}




/* ==================== 线程函数 ==================== */
static void soil_sensor_thread_entry(void *parameter)
{
    LOG_I("Soil sensor thread started");

    while (sensors_running)
    {
        read_soil_sensor();
        rt_thread_mdelay(THREAD_SOIL_TICK);
    }

    LOG_I("Soil sensor thread stopped");
}

static void ds18b20_sensor_thread_entry(void *parameter)
{
    LOG_I("DS18B20 sensor thread started");

    while (sensors_running)
    {
        read_ds18b20_sensor();
        rt_thread_mdelay(THREAD_DS18B20_TICK);
    }

    LOG_I("DS18B20 sensor thread stopped");
}

static void sht3x_sensor_thread_entry(void *parameter)
{
    LOG_I("SHT3X sensor thread started");

    while (sensors_running)
    {
        read_sht3x_sensor();
        rt_thread_mdelay(THREAD_SHT3X_TICK);
    }

    LOG_I("SHT3X sensor thread stopped");
}

static void bh1750_sensor_thread_entry(void *parameter)
{
    LOG_I("BH1750 sensor thread started");

    while (sensors_running)
    {
        read_bh1750_sensor();
        rt_thread_mdelay(THREAD_BH1750_TICK);
    }

    LOG_I("BH1750 sensor thread stopped");
}

static void sgp30_sensor_thread_entry(void *parameter)
{
    LOG_I("SGP30 sensor thread started");

    while (sensors_running)
    {
        read_sgp30_sensor();
        rt_thread_mdelay(THREAD_SGP30_TICK);
    }

    LOG_I("SGP30 sensor thread stopped");
}

/* AI 识别线程入口 */
static void ai_recognition_thread_entry(void *parameter)
{
    if (uart_dev == RT_NULL || rx_sem == RT_NULL) {
        LOG_E("AI UART not initialized, thread exits");
        return;
    }
    char read_buf[128];
    int read_len;

    while (sensors_running) {
        /* 1. 等待信号量 */
        if (rt_sem_take(rx_sem, RT_WAITING_FOREVER) != RT_EOK)
            continue;

        /* 2. 批量读取数据 */
        read_len = rt_device_read(uart_dev, -1, read_buf, sizeof(read_buf) - 1);
        if (read_len <= 0)
            continue;

        /* 3. 追加到行缓冲区并处理溢出 */
        int copy_len = read_len;
        if (line_pos + copy_len >= sizeof(line_buf)) {
            LOG_W("Line buffer overflow, discard old data");
            line_pos = 0;
        }
        rt_memcpy(line_buf + line_pos, read_buf, copy_len);
        line_pos += copy_len;

        /* 4. 逐行解析（以 '\n' 分隔） */
        char *p_start = line_buf;
        char *p_end;
        while ((p_end = strchr(p_start, '\n')) != RT_NULL) {
            *p_end = '\0';   // 将换行符替换为字符串结束符
            // 去除可能存在的回车符 '\r'
            char *line = p_start;
            if (p_end > p_start && *(p_end - 1) == '\r')
                *(p_end - 1) = '\0';

            parse_ai_message(line);   // 处理一行数据

            p_start = p_end + 1;      // 移动到下一行开头
        }

        /* 5. 处理剩余未完成的行数据 */
        if (p_start != line_buf) {
            int remaining = line_pos - (p_start - line_buf);
            if (remaining > 0) {
                rt_memmove(line_buf, p_start, remaining);
            }
            line_pos = remaining;
        }
    }
}

/* ==================== 线程管理函数 ==================== */
int sensors_threads_init(void)
{
    LOG_I("Starting sensor threads...");

    sensors_running = RT_TRUE;

    /* 初始化传感器硬件 */
    if (env_sensors_init() != RT_EOK)
    {
        LOG_W("Some environment sensors initialization failed");
    }

    /* 为土壤传感器创建线程 */
    soil_thread = rt_thread_create("soil_sensor",
        soil_sensor_thread_entry,
        RT_NULL,
        THREAD_SOIL_STACK_SIZE,
        THREAD_SOIL_PRIORITY,
        10);
    if (soil_thread != RT_NULL)
    {
        rt_thread_startup(soil_thread);
    }
    else
    {
        LOG_E("Failed to create soil sensor thread");
    }

    /* 创建 DS18B20 线程 */
    ds18b20_thread = rt_thread_create("ds18b20",
        ds18b20_sensor_thread_entry,
        RT_NULL,
        THREAD_DS18B20_STACK_SIZE,
        THREAD_DS18B20_PRIORITY,
        10);
    if (ds18b20_thread != RT_NULL)
    {
        rt_thread_startup(ds18b20_thread);
    }
    else
    {
        LOG_E("Failed to create DS18B20 thread");
    }

    /* 创建环境传感器线程 */
    sht3x_thread = rt_thread_create("sht3x_sensor",
        sht3x_sensor_thread_entry,
        RT_NULL,
        THREAD_SHT3X_STACK_SIZE,
        THREAD_SHT3X_PRIORITY,
        10);
    if (sht3x_thread != RT_NULL)
    {
        rt_thread_startup(sht3x_thread);
    }
    else
    {
        LOG_E("Failed to create SHT3X thread");
    }

    bh1750_thread = rt_thread_create("bh1750_sensor",
        bh1750_sensor_thread_entry,
        RT_NULL,
        THREAD_BH1750_STACK_SIZE,
        THREAD_BH1750_PRIORITY,
        10);
    if (bh1750_thread != RT_NULL)
    {
        rt_thread_startup(bh1750_thread);
    }
    else
    {
        LOG_E("Failed to create BH1750 thread");
    }

    sgp30_thread = rt_thread_create("sgp30_sensor",
        sgp30_sensor_thread_entry,
        RT_NULL,
        THREAD_SGP30_STACK_SIZE,
        THREAD_SGP30_PRIORITY,
        10);
    if (sgp30_thread != RT_NULL)
    {
        rt_thread_startup(sgp30_thread);
    }
    else
    {
        LOG_E("Failed to create SGP30 thread");
    }

    /* 创建 AI 识别线程 */
    ai_thread = rt_thread_create("ai_recog",
        ai_recognition_thread_entry,
        RT_NULL,
        THREAD_AI_STACK_SIZE,
        THREAD_AI_PRIORITY,
        10);
    if (ai_thread != RT_NULL)
    {
        rt_thread_startup(ai_thread);
    }
    else
    {
        LOG_E("Failed to create AI recognition thread");
    }

    LOG_I("Sensor threads started successfully");
    return RT_EOK;

}

void sensors_threads_stop(void)
{
    LOG_I("Stopping sensor threads...");

    sensors_running = RT_FALSE;

    /* 等待线程结束 */
    rt_thread_mdelay(100);

    /* 清理土壤传感器线程资源 */
    if (soil_thread != RT_NULL)
    {
        rt_thread_delete(soil_thread);
        soil_thread = RT_NULL;
    }

    /* 清理 DS18B20 线程 */
    if (ds18b20_thread != RT_NULL)
    {
        rt_thread_delete(ds18b20_thread);
        ds18b20_thread = RT_NULL;
    }

    /* 清理环境传感器线程资源 */
    if (sht3x_thread != RT_NULL)
    {
        rt_thread_delete(sht3x_thread);
        sht3x_thread = RT_NULL;
    }

    if (bh1750_thread != RT_NULL)
    {
        rt_thread_delete(bh1750_thread);
        bh1750_thread = RT_NULL;
    }

    if (sgp30_thread != RT_NULL)
    {
        rt_thread_delete(sgp30_thread);
        sgp30_thread = RT_NULL;
    }

    /* 清理 AI 识别线程 */
    if (ai_thread != RT_NULL)
    {
        rt_thread_delete(ai_thread);
        ai_thread = RT_NULL;
    }

    /* 删除信号量 */
    if (rx_sem != RT_NULL) {
        rt_sem_delete(rx_sem);
        rx_sem = RT_NULL;
    }


    LOG_I("Sensor threads stopped");
}

// 添加一个 MSH 命令，将 Flash 地址写回全 0xFF（表示未初始化）
static void clear_sgp30_baseline(void)
{
    uint64_t *addr = (uint64_t*)SGP30_BASELINE_FLASH_ADDR;
    *addr = 0xFFFFFFFFFFFFFFFF;
    rt_kprintf("SGP30 baseline cleared\n");
}
MSH_CMD_EXPORT(clear_sgp30_baseline, Clear SGP30 baseline in Flash);


