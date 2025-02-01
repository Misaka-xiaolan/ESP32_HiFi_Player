//
// Created by Misaka on 24-10-16.
//

#ifndef _ST7789_DRIVER_H_
#define _ST7789_DRIVER_H_

#include "driver/gpio.h"
#include "esp_err.h"

typedef void(*lcd_flush_done_cb)(void *param);

typedef struct
{
    gpio_num_t mosi;       //数据
    gpio_num_t clk;        //时钟
    gpio_num_t cs;         //片选
    gpio_num_t dc;         //命令
    gpio_num_t rst;        //复位
    gpio_num_t bl;         //背光
    uint32_t spi_fre;    //spi总线速率
    uint16_t width;      //宽
    uint16_t height;     //长
    uint8_t spin;       //选择方向(0不旋转，1顺时针旋转90, 2旋转180，3顺时针旋转270)
    lcd_flush_done_cb done_cb;    //数据传输完成回调函数
    void *cb_param;   //回调函数参数
} st7789_cfg_t;


/** st7789初始化
 * @param st7789_cfg_t  接口参数
 * @return 成功或失败
*/
esp_err_t st7789_driver_hw_init(st7789_cfg_t *cfg);

/** st7789写入显示数据
 * @param x1,x2,y1,y2:显示区域
 * @return 无
*/
void st7789_flush(int x1, int x2, int y1, int y2, void *data);

/** 控制背光
 * @param enable 是否使能背光
 * @return 无
*/
void st7789_lcd_backlight_set(bool enable);
bool st7789_lcd_backlight_get(void);

#endif

