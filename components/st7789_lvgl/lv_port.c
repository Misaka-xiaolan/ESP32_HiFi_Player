//
// Created by Misaka on 24-10-17.
//

#include "lv_port.h"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st7789_driver.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "lv_port_indev.h"

/*
 * 1、初始化和注册LVGL显示驱动
 * 2、初始化和注册LVGL触摸驱动
 * 3、初始化st7789硬件接口
 * 4、初始化触摸硬件接口
 * 5、提供一个定时器给LVGL使用
 * */

#define TAG "lv_port"

static lv_disp_drv_t disp_drv;

void lv_Flush_Done_cb(void *param)
{
    lv_disp_flush_ready(&disp_drv);
}

void Disp_Flush(struct _lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    st7789_flush(area->x1, area->x2 + 1, area->y1, area->y2 + 1, color_p);
}

void LVGL_Display_Init(void)
{
    static lv_disp_draw_buf_t disp_buf;
    const size_t disp_buf_size = LCD_WIDTH * (LCD_HEIGHT / 7);

    //lvgl单缓存设计
    lv_color_t *disp1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!disp1)
    {
        ESP_LOGE(TAG, "display buf malloc failed!");
        return;
    }
    lv_disp_draw_buf_init(&disp_buf, disp1, NULL, disp_buf_size);//初始化显示内存

    lv_disp_drv_init(&disp_drv); //初始化显示驱动
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.offset_y = 0;  //竖屏时，调整为20
    disp_drv.offset_x = 20; //横屏时，调整为20
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = Disp_Flush;
    lv_disp_drv_register(&disp_drv);

}

void ST7789_Hw_Init(void)
{
    st7789_cfg_t st7789_cfg = {
            .bl = GPIO_NUM_26,
            .clk = GPIO_NUM_18,
            .cs = GPIO_NUM_27,
            .dc = GPIO_NUM_25,
            .mosi = GPIO_NUM_23,
            .rst = /*GPIO_NUM_36*/-1,
            .spi_fre = 80 * 1000 * 1000,
            .height = LCD_HEIGHT,
            .width = LCD_WIDTH,
            .spin = 3,
            .done_cb = lv_Flush_Done_cb,
            .cb_param = &disp_drv
    };

    st7789_driver_hw_init(&st7789_cfg);
}

void lv_Timer_cb(void *arg)
{
    lv_tick_inc(*(uint32_t *) arg);
}

void lv_Tick_Init(void)
{
    static uint32_t timer_interval = 5;
    const esp_timer_create_args_t arg = {
            .arg = &timer_interval,
            .callback = lv_Timer_cb,
            .name = "",
            .dispatch_method = ESP_TIMER_TASK,
            .skip_unhandled_events = true
    };

    esp_timer_handle_t timer_handle;
    esp_timer_create(&arg, &timer_handle);
    esp_timer_start_periodic(timer_handle, timer_interval * 1000);
}

void lv_Port_Init(void)
{
    lv_init();
    ST7789_Hw_Init();
    LVGL_Display_Init();
    lv_port_indev_init();
    lv_Tick_Init();
}
