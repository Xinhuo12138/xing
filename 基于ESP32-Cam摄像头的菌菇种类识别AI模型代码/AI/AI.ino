/* Edge Impulse Arduino examples
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// These sketches are tested with 2.0.4 ESP32 Arduino Core
// https://github.com/espressif/arduino-esp32/releases/tag/2.0.4

/* Includes ------------------------------------------+---------------------- */
#include <AI_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include <LovyanGFX.hpp>
#include <SPI.h>

#include "esp_camera.h"


// 定义面板配置类
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7735S _panel_instance;  // ST7735S 驱动
    lgfx::Bus_SPI       _bus_instance;    // SPI 总线

public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = VSPI_HOST;     // 使用 VSPI 主机
            cfg.spi_mode = 0;             // SPI 模式 0
            cfg.freq_write = 10000000;    // 10MHz
            cfg.freq_read  = 10000000;
            cfg.spi_3wire  = true;
            cfg.use_lock   = true;
            cfg.dma_channel = 1;
            cfg.pin_sclk = 12;
            cfg.pin_mosi = 13;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 14;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = 2;
            cfg.pin_rst  = 15;
            cfg.panel_width  = 128;
            cfg.panel_height = 160;
            cfg.memory_width = 128;   // ★ 关键：ST7735 的 RAM 行宽通常是 132
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            // 尝试常见的色彩顺序
            cfg.invert    = false;   // 开启反转试试；如果不行就换 false
            cfg.rgb_order = true;  // false=BGR, true=RGB
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }
};

LGFX lcd;


// Select camera model - find more camera models in camera_pins.h file here
// https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// 在文件开头添加显示区域定义
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 128          // 图像显示高度（剩余32行用于文字）
#define DISPLAY_TXT_Y  128           // 文字起始Y坐标

// 全局静态显示缓冲区（避免重复分配）
static uint16_t display_buf[DISPLAY_WIDTH * DISPLAY_HEIGHT];

const char* class_names[] = {"HongGu", "HuangGu", "LanGu", "LvGu", "YunGu"}; 

/* Constant defines -------------------------------------------------------- */
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS           240
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS           240
#define EI_CAMERA_FRAME_BYTE_SIZE                 3

/* Private variables ------------------------------------------------------- */
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static bool is_initialised = false;
uint8_t *snapshot_buf;   // 用于推理图像（缩放后）
uint8_t *raw_buf;        // 新增：用于原始图像

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_240X240,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 5, //0-63 lower number means higher quality
    .fb_count = 1,       //if more than one, i2s runs in continuous mode. Use only with JPEG
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

/* Function definitions ------------------------------------------------------- */
bool ei_camera_init(void);
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) ;
void convert_rgb888_to_rgb565_resize(uint8_t *src, uint16_t *dst,uint32_t src_w, uint32_t src_h,uint32_t dst_w, uint32_t dst_h);


/**
* @brief      Arduino setup function
*/
void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    //comment out the below line to start inference immediately after upload
    while (!Serial);
    Serial.println("Edge Impulse Inferencing Demo");
    pinMode(4, OUTPUT);  // 将引脚2设置为输出模式
    digitalWrite(4, HIGH);  // 将引脚2设置为高电平
    delay(500);
    digitalWrite(4, LOW);  // 将引脚2设置为低电平

    lcd.init();
    lcd.setRotation(2);
    lcd.setColorDepth(16);
    lcd.setSwapBytes(true);   // 试试 true 或 false

    // ★ 强制写入与 STM32 完全相同的 MADCTL 值 ★
    lcd.writeCommand(0x36);
    lcd.writeData(0xC0);

    raw_buf = (uint8_t*) heap_caps_malloc(320 * 240 * 3, MALLOC_CAP_SPIRAM);
    snapshot_buf = (uint8_t*) heap_caps_malloc(EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3, MALLOC_CAP_SPIRAM);

    if (!raw_buf || !snapshot_buf) {
        Serial.println("PSRAM allocation failed! Check PSRAM settings.");
        while (1) delay(1000);
    }
    
    if (ei_camera_init() == false) {
        ei_printf("Failed to initialize Camera!\r\n");
    }
    else {
        ei_printf("Camera initialized\r\n");
    }

    ei_printf("\nStarting continious inference in 2 seconds...\n");
    ei_sleep(2000);
}


/**
* @brief      Get data and run inferencing
*
* @param[in]  debug  Get debug info if true
*/
void loop()
{
    //Serial.println("1: start capture...");
    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    if (ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf) == false) {
        ei_printf("Capture failed\n");
        return;
    }

    //Serial.println("2: capture ok, start inference...");

     // 运行推理（使用 snapshot_buf）
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        return;
    }


    // 改为打印检测框
    //ei_printf("Object detection bounding boxes:\r\n");
    Serial.print("BEGIN\r\n");               // 帧头
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;   // 忽略低置信度检测
         ei_printf("%s: %.5f\r\n", bb.label, bb.value);
    }
    Serial.print("END\r\n");                  // 帧尾
    
    // 显示原始图像（raw_buf）及推理结果
    display_image_and_results(raw_buf, EI_CAMERA_RAW_FRAME_BUFFER_COLS, EI_CAMERA_RAW_FRAME_BUFFER_ROWS, result);
}


// 新增函数：在 ST7735 上显示图像和推理结果
void display_image_and_results(uint8_t* img_buf, uint32_t src_w, uint32_t src_h, ei_impulse_result_t &result){
    // 1. 转换图像（snapshot_buf -> display_buf）
    convert_rgb888_to_rgb565_resize(img_buf, display_buf,
                                    src_w,
                                    src_h,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // 2. 手动设置显示窗口（全屏 128x128）
    lcd.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // 3. 逐行发送像素数据（避免任何自动地址跳转错误）
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        lcd.pushPixels(display_buf + y * DISPLAY_WIDTH, DISPLAY_WIDTH);
    }
    // 4. 清除文字区域（下方 32 行）
    lcd.fillRect(0, DISPLAY_TXT_Y, DISPLAY_WIDTH, 160 - DISPLAY_TXT_Y, TFT_BLACK);

    // 5. 设置文本样式
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(1);      // 小字号，可显示多行
    //lcd.setCursor(5, DISPLAY_TXT_Y);

    // 3. 显示检测框信息（最多显示3个，避免溢出）
    int count = 0;
    for (uint32_t i = 0; i < result.bounding_boxes_count && count < 3; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;
        lcd.setCursor(0, DISPLAY_TXT_Y + count * 8);
        // 显示格式：标签 置信度 [x,y,w,h]
        lcd.printf("%s %.2f [%u,%u,%u,%u]", bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);
        count++;
    }

    // 如果无检测框，可显示“No object”
    if (count == 0) {
        lcd.setCursor(0, DISPLAY_TXT_Y);
        lcd.print("No object");
    }


}

/**
 * @brief   Setup image sensor & start streaming
 *
 * @retval  false if initialisation failed
 */
bool ei_camera_init(void) {

    if (is_initialised) return true;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
      Serial.printf("Camera init failed with error 0x%x\n", err);
      return false;
    }

    sensor_t * s = esp_camera_sensor_get();
    // 初始传感器可能垂直翻转，颜色偏饱和，这里进行调整
if (s->id.PID == OV2640_PID) {   // 添加对 OV2640 的处理
    s->set_sharpness(s, 4);       // 锐度 0~6，4 或 5 较好
    s->set_saturation(s, 3);      // 尝试 1（正常）或 2（更高）设置饱和度
    s->set_brightness(s,-2);      // 亮度可保持默认
    s->set_contrast(s, 4);        // 对比度保持默认
    s->set_awb_gain(s, 0);        // 开启自动白平衡（可能默认已开）
}
#if defined(CAMERA_MODEL_M5STACK_WIDE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);

#endif

    is_initialised = true;
    return true;
}

/**
 * @brief      Stop streaming of sensor data
 */
void ei_camera_deinit(void) {

    //deinitialize the camera
    esp_err_t err = esp_camera_deinit();

    if (err != ESP_OK)
    {
        ei_printf("Camera deinit failed\n");
        return;
    }

    is_initialised = false;
    return;
}


/**
 * @brief      Capture, rescale and crop image
 *
 * @param[in]  img_width     width of output image
 * @param[in]  img_height    height of output image
 * @param[in]  out_buf       pointer to store output image, NULL may be used
 *                           if ei_camera_frame_buffer is to be used for capture and resize/cropping.
 *
 * @retval     false if not initialised, image captured, rescaled or cropped failed
 *
 */
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    bool do_resize = false;

    if (!is_initialised) {
        ei_printf("ERR: Camera is not initialized\r\n");
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
        ei_printf("Camera capture failed\n");
        return false;
    }
    // 1. JPEG → RGB888 存入 raw_buf（全局变量，需已分配）
   bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, raw_buf);

   esp_camera_fb_return(fb);

   if(!converted){
       ei_printf("Conversion failed\n");
       return false;
   }

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS)
        || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        do_resize = true;
    }
     // 2. 若目标尺寸与原始不同，则缩放 raw_buf → out_buf
    if (do_resize) {
            // 使用自实现的安全缩放，替代 SDK 中可能导致内存越界的函数
        resize_rgb888(raw_buf,
                  EI_CAMERA_RAW_FRAME_BUFFER_COLS, EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
                  out_buf,
                  img_width, img_height);
    }else {
        // ★ 新增：尺寸相同，直接复制数据 ★
        memcpy(out_buf, raw_buf, img_width * img_height * 3);
    }

    return true;
}

// 安全的 RGB888 缩放（最近邻）
void resize_rgb888(uint8_t *src, uint32_t src_w, uint32_t src_h,
                   uint8_t *dst, uint32_t dst_w, uint32_t dst_h) {
    for (uint32_t y = 0; y < dst_h; y++) {
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t src_x = (x * src_w) / dst_w;
            uint32_t src_y = (y * src_h) / dst_h;
            uint32_t src_idx = (src_y * src_w + src_x) * 3;
            uint32_t dst_idx = (y * dst_w + x) * 3;
            dst[dst_idx + 0] = src[src_idx + 0];
            dst[dst_idx + 1] = src[src_idx + 1];
            dst[dst_idx + 2] = src[src_idx + 2];
        }
    }
}

// 新增函数：将 RGB888 缩放并转换为 RGB565（最近邻插值）
void convert_rgb888_to_rgb565_resize(uint8_t *src, uint16_t *dst,
                                     uint32_t src_w, uint32_t src_h,
                                     uint32_t dst_w, uint32_t dst_h) {
    float x_ratio = (float)(src_w - 1) / dst_w;
    float y_ratio = (float)(src_h - 1) / dst_h;

    for (uint32_t y = 0; y < dst_h; y++) {
        float src_y = y * y_ratio;
        uint32_t y0 = (uint32_t)src_y;
        uint32_t y1 = (y0 + 1 < src_h) ? y0 + 1 : y0;
        float dy = src_y - y0;

        for (uint32_t x = 0; x < dst_w; x++) {
            float src_x = x * x_ratio;
            uint32_t x0 = (uint32_t)src_x;
            uint32_t x1 = (x0 + 1 < src_w) ? x0 + 1 : x0;
            float dx = src_x - x0;

            // 4 个邻近像素的索引（BGR 顺序）
            uint32_t idx00 = (y0 * src_w + x0) * 3;
            uint32_t idx01 = (y0 * src_w + x1) * 3;
            uint32_t idx10 = (y1 * src_w + x0) * 3;
            uint32_t idx11 = (y1 * src_w + x1) * 3;

            // 对每个通道进行双线性插值
            uint8_t b = (1 - dy) * ((1 - dx) * src[idx00 + 0] + dx * src[idx01 + 0]) +
                        dy * ((1 - dx) * src[idx10 + 0] + dx * src[idx11 + 0]);
            uint8_t g = (1 - dy) * ((1 - dx) * src[idx00 + 1] + dx * src[idx01 + 1]) +
                        dy * ((1 - dx) * src[idx10 + 1] + dx * src[idx11 + 1]);
            uint8_t r = (1 - dy) * ((1 - dx) * src[idx00 + 2] + dx * src[idx01 + 2]) +
                        dy * ((1 - dx) * src[idx10 + 2] + dx * src[idx11 + 2]);

            // 注意：src 是 BGR 顺序，所以 r = idx+2, g = idx+1, b = idx+0
            // 组合为 RGB565
            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            dst[y * dst_w + x] = rgb565;
        }
    }
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    // we already have a RGB888 buffer, so recalculate offset into pixel index
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        // Swap BGR to RGB here
        // due to https://github.com/espressif/esp32-camera/issues/379
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];

        // go to the next pixel
        out_ptr_ix++;
        pixel_ix+=3;
        pixels_left--;
    }
    // and done!
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
