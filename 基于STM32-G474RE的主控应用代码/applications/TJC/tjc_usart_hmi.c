/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-31     17625       the first version
 */
/**
使用注意事项:
    1.将tjc_usart_hmi.c和tjc_usart_hmi.h 分别导入工程
    2.在需要使用的函数所在的头文件中添加 #include "tjc_usart_hmi.h"
    3.使用前请将 HAL_UART_Transmit_IT() 这个函数改为你的单片机的串口发送单字节函数


*/
#include <string.h>
#include <stdio.h>
#include "tjc_usart_hmi.h"


// RT-Thread 设备句柄
struct rt_device *tjc_uart_dev = RT_NULL;
struct rt_ringbuffer *rx_ringbuffer = RT_NULL;

typedef struct
{
    uint16_t Head;
    uint16_t Tail;
    uint16_t Length;
    uint8_t  Ring_data[RINGBUFFER_LEN];
}RingBuffer_t;

RingBuffer_t ringBuffer;    //创建一个ringBuffer的缓冲区



/********************************************************
函数名：        intToStr
日期：     2024.09.18
功能：     将整形转换为字符串
输入参数：       要转换的整形数据,输出的字符串数组
返回值：        无
修改记录：
**********************************************************/
void intToStr(int num, char* str) {
    int i = 0;
    int isNegative = 0;

    // 处理负数
    if (num < 0) {
        isNegative = 1;
        num = -num;
    }

    // 提取每一位数字
    do {
        str[i++] = (num % 10) + '0';
        num /= 10;
    } while (num);

    // 如果是负数，添加负号
    if (isNegative) {
        str[i++] = '-';
    }

    // 添加字符串终止符
    str[i] = '\0';

    // 反转字符串
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
    return ;
}

/**
 * @brief 在 TJC 屏上动态显示中文（自动转换 UTF-8 → GB2312）
 * @param widget 控件名称（如 "label.status"）
 * @param utf8_str UTF-8 编码的字符串（可以是变量或字面量）
 *
 * @note 内部自动转换并发送，无需手动管理编码
 */
void tjc_show_chinese(const char *widget, const char *utf8_str)
{
    if (!widget || !utf8_str) return;

    static char gb_buffer[128];  // 静态缓冲区，避免栈溢出
    uint32_t out_len = 0;

    int ret = utf8_to_gb2312((const uint8_t*)utf8_str, strlen(utf8_str),
                             (uint8_t*)gb_buffer, &out_len);
    if (ret == 0) {
        gb_buffer[out_len] = '\0';
        tjc_send_txt(widget, "txt", gb_buffer);
    } else {
        //LOG_E("UTF8 to GB2312 failed for: %s", utf8_str);
        // 可选：发送空字符串或默认提示
        tjc_send_txt(widget, "txt", "");
    }
}

/********************************************************
函数名：        uart_send_char
日期：     2024.09.18
功能：     串口发送单个字符
输入参数：       要发送的单个字符
返回值：        无
修改记录：
**********************************************************/
void uart_send_char(char ch)
{
    if (tjc_uart_dev == RT_NULL) {
        return;
    }
    uint8_t data = (uint8_t)ch;
    rt_device_write(tjc_uart_dev, 0, &data, 1);
}

/********************************************************
函数名：        uart_send_string
日期：        2024.09.18
功能：        串口发送字符串
输入参数：        要发送的字符串
返回值：        无
修改记录：
**********************************************************/
void uart_send_string(char* str)
{
    if (tjc_uart_dev == RT_NULL || str == RT_NULL) {
        return;
    }

    int len = rt_strlen(str);
    if (len > 0) {
        rt_device_write(tjc_uart_dev, 0, str, len);
    }
}

/********************************************************
函数名：        tjc_send_string
日期：     2024.09.18
功能：     串口发送字符串和结束符
输入参数：       要发送的字符串
返回值：        无
示例:         tjc_send_val("n0", "val", 100); 发出的数据就是 n0.val=100
修改记录：
**********************************************************/
void tjc_send_string(char* str)
{
    if (tjc_uart_dev == RT_NULL || str == RT_NULL) {
        return;
    }

    uart_send_string(str);

    // 发送结束符
    uint8_t end_bytes[3] = {0xFF, 0xFF, 0xFF};
    rt_device_write(tjc_uart_dev, 0, end_bytes, 3);
}

/********************************************************
函数名：        tjc_send_txt
日期：     2024.09.18
功能：     串口发送字符串和结束符
输入参数：       要发送的字符串
返回值：        无
示例:         tjc_send_txt("t0", "txt", "ABC"); 发出的数据就是t0.txt="ABC"
修改记录：
**********************************************************/
void tjc_send_txt(char* objname, char* attribute, char* txt)
{
    if (tjc_uart_dev == RT_NULL) {
        return;
    }

    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_string("=\"");
    uart_send_string(txt);
    uart_send_char('\"');
    // 发送结束符
    uint8_t end_bytes[3] = {0xFF, 0xFF, 0xFF};
    rt_device_write(tjc_uart_dev, 0, end_bytes, 3);

}


/********************************************************
函数名：        tjc_send_val
日期：     2024.09.18
功能：     串口发送字符串和结束符
输入参数：       要发送的字符串
返回值：        无
修改记录：
**********************************************************/
void tjc_send_val(char* objname, char* attribute, int val)
{
    if (tjc_uart_dev == RT_NULL) {
           return;
    }

    //拼接字符串,比如n0.val=123
    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_char('=');
    //C语言中整形的取值范围是：“-2147483648 ~ 2147483647”, 最长为-2147483648,加上结束符\0一共12个字符
    char txt[12]="";
    intToStr(val, txt);
    uart_send_string(txt);
    // 发送结束符
    uint8_t end_bytes[3] = {0xFF, 0xFF, 0xFF};
    rt_device_write(tjc_uart_dev, 0, end_bytes, 3);
}

/********************************************************
函数名：        tjc_send_nstring
日期：     2024.09.18
功能：     串口发送字符串和结束符
输入参数：       要发送的字符串,字符串长度
返回值：        无
修改记录：
**********************************************************/
void tjc_send_nstring(char* str, unsigned char str_length)
{
    if (tjc_uart_dev == RT_NULL || str == RT_NULL) {
        return;
    }
    //当前字符串地址不在结尾 并且 字符串首地址不为空
    for (int var = 0; var < str_length; ++var)
    {
        //发送字符串首地址中的字符，并且在发送完成之后首地址自增
        uart_send_char(*str++);
    }
    // 发送结束符
    uint8_t end_bytes[3] = {0xFF, 0xFF, 0xFF};
    rt_device_write(tjc_uart_dev, 0, end_bytes, 3);
}


/********************************************************
函数名：        tjc_uart_rx_ind
日期：        2024.09.18
功能：        RT-Thread 串口接收回调函数
输入参数：        dev: 设备句柄, size: 数据大小
返回值：        无
修改记录：
**********************************************************/
static rt_err_t tjc_uart_rx_ind(rt_device_t dev, rt_size_t size)
{
    static uint8_t buffer[64];
//    static rt_size_t buffer_index = 0;

    if (size > 0) {
            // 读取串口数据
            rt_size_t read_size = rt_device_read(dev, 0, buffer,(size > 64) ? 64 : size);

            // 将数据写入环形缓冲区
            for (rt_size_t i = 0; i < read_size; i++) {
                write1ByteToRingBuffer(buffer[i]);
            }
    }
    return RT_EOK;
}


/********************************************************
函数名：        tjc_uart_init
日期：        2024.09.18
功能：        初始化TJC串口
输入参数：        无
返回值：        RT_EOK: 成功, 其他: 失败
修改记录：
**********************************************************/
rt_err_t tjc_uart_init(void)
{
    // 初始化环形缓冲区
    initRingBuffer();

    // 查找串口设备
    tjc_uart_dev = rt_device_find(TJC_UART_NAME);
    if (tjc_uart_dev == RT_NULL) {
        rt_kprintf("TJC UART device not found!\n");
        return -RT_ERROR;
    }

    // 以读写和中断接收方式打开串口设备
    rt_err_t err = rt_device_open(tjc_uart_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (err != RT_EOK) {
        rt_kprintf("Failed to open TJC UART device!\n");
        return err;
    }

    // 设置串口参数（根据TJC屏要求设置，通常为115200,8,N,1）
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = BAUD_RATE_115200;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity = PARITY_NONE;
    config.bufsz = 256;  // 增加缓冲区大小
    rt_device_control(tjc_uart_dev, RT_DEVICE_CTRL_CONFIG, &config);

    // 设置接收回调函数
    rt_device_set_rx_indicate(tjc_uart_dev, tjc_uart_rx_ind);

    rt_kprintf("TJC UART initialized successfully!\n");
    return RT_EOK;
}

////********************************************************
//函数名：    HAL_UART_RxCpltCallback
//日期：     2022.10.08
//功能：     串口接收中断,将接收到的数据写入环形缓冲区
//输入参数：
//返回值：        void
//修改记录：
//**********************************************************/
//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//
//    if(huart->Instance == TJC_UART_INS) // 判断是由哪个串口触发的中断
//    {
//        write1ByteToRingBuffer(RxBuffer[0]);
//        HAL_UART_Receive_IT(&TJC_UART,RxBuffer,1);      // 重新使能串口2接收中断
//    }
//    return;
//}



/********************************************************
函数名：        initRingBuffer
日期：     2022.10.08
功能：     初始化环形缓冲区
输入参数：
返回值：        void
修改记录：
**********************************************************/
void initRingBuffer(void)
{
    //初始化相关信息
    ringBuffer.Head = 0;
    ringBuffer.Tail = 0;
    ringBuffer.Length = 0;
    rt_memset(ringBuffer.Ring_data, 0, RINGBUFFER_LEN);
}



/********************************************************
函数名：        write1ByteToRingBuffer
日期：     2022.10.08
功能：     往环形缓冲区写入数据
输入参数：       要写入的1字节数据
返回值：        void
修改记录：
**********************************************************/
void write1ByteToRingBuffer(uint8_t data)
{
    if(ringBuffer.Length >= RINGBUFFER_LEN){            //判断缓冲区是否已满
        // 缓冲区已满，丢弃最旧的数据
        ringBuffer.Head = (ringBuffer.Head + 1) % RINGBUFFER_LEN;
        ringBuffer.Length--;
    }
    ringBuffer.Ring_data[ringBuffer.Tail]=data;
    ringBuffer.Tail = (ringBuffer.Tail+1)%RINGBUFFER_LEN;//防止越界非法访问
    ringBuffer.Length++;
}




/********************************************************
函数名：        deleteRingBuffer
作者：
日期：     2022.10.08
功能：     删除串口缓冲区中相应长度的数据
输入参数：       要删除的长度
返回值：        void
修改记录：
**********************************************************/
void deleteRingBuffer(uint16_t size)
{
    if(size >= ringBuffer.Length)
    {
        initRingBuffer();
        return;
    }
    for(int i = 0; i < size; i++)
    {
        ringBuffer.Head = (ringBuffer.Head+1)%RINGBUFFER_LEN;//防止越界非法访问
        ringBuffer.Length--;
        return;
    }

}



/********************************************************
函数名：        read1ByteFromRingBuffer
作者：
日期：     2022.10.08
功能：     从串口缓冲区读取1字节数据
输入参数：       position:读取的位置
返回值：        所在位置的数据(1字节)
修改记录：
**********************************************************/
uint8_t read1ByteFromRingBuffer(uint16_t position)
{
    if (position >= ringBuffer.Length) {
        return 0;
    }

    uint16_t realPosition = (ringBuffer.Head + position) % RINGBUFFER_LEN;

    return ringBuffer.Ring_data[realPosition];
}




/********************************************************
函数名：        getRingBufferLength
作者：
日期：     2022.10.08
功能：     获取串口缓冲区的数据数量
输入参数：
返回值：        串口缓冲区的数据数量
修改记录：
**********************************************************/
uint16_t getRingBufferLength()
{
    return ringBuffer.Length;
}


/********************************************************
函数名：        isRingBufferOverflow
作者：
日期：     2022.10.08
功能：     判断环形缓冲区是否已满
输入参数：
返回值：        0:环形缓冲区已满 , 1:环形缓冲区未满
修改记录：
**********************************************************/
uint8_t isRingBufferOverflow()
{
    return ringBuffer.Length < RINGBUFFER_LEN;
}


