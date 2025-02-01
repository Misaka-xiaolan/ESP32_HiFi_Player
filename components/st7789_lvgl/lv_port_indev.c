/**
 * @file lv_port_indev_templ.c
 *
 */

/*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include "lv_port_indev.h"
#include "lvgl.h"
#include "../../main/freertos_task.h"
#include "st7789_driver.h"
#include "../ui/ui.h"
//#include "../../main/audioplay.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void keypad_init(void);

static void keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

static uint32_t keypad_get_key(void);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t *indev_keypad;


/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    static lv_indev_drv_t indev_drv;

    /*------------------
     * Keypad
     * -----------------*/

    /*Initialize your keypad or keyboard if you have*/
    keypad_init();

    /*Register a keypad input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = keypad_read;
    indev_keypad = lv_indev_drv_register(&indev_drv);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_keypad, group);`*/
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Keypad
 * -----------------*/

/*Initialize your keypad*/
static void keypad_init(void)
{
    /*Your code comes here*/
    gpio_config_t conf = {};
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_DISABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.pin_bit_mask = (1ULL << KEY_UP_PIN);
    gpio_config(&conf);
    conf.pin_bit_mask = (1ULL << KEY_DOWN_PIN);
    gpio_config(&conf);
    conf.pin_bit_mask = (1ULL << KEY_LEFT_PIN);
    gpio_config(&conf);
    conf.pin_bit_mask = (1ULL << KEY_RIGHT_PIN);
    gpio_config(&conf);
    conf.pin_bit_mask = (1ULL << KEY_CENTER_PIN);
    gpio_config(&conf);
}

/*Will be called by the library to read the mouse*/
static void keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;


    /*Get whether the a key is pressed and save the pressed key*/
    uint32_t act_key = keypad_get_key();
    if (act_key != 0)
    {
        gptimer_set_raw_count(lcd_off_timer, 0);
        data->state = LV_INDEV_STATE_PR;
        /*Translate the keys to LVGL control characters according to your key definitions*/
        switch (act_key)
        {
            case 1:
                act_key = LV_KEY_LEFT;
                break;
            case 2:
                act_key = LV_KEY_RIGHT;
                break;
            case 3:
                act_key = LV_KEY_LEFT;
                break;
            case 4:
                act_key = LV_KEY_RIGHT;
                break;
            case 5:
                act_key = LV_KEY_ENTER;
                break;
        }

        last_key = act_key;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }

    data->key = last_key;
}

/*Get the currently being pressed key.  0 if no key is pressed*/
static uint32_t keypad_get_key(void)
{
    /*Your code comes here*/
    if (gpio_get_level(KEY_UP_PIN) == 0)
    {
        printf("key up pressed\n");
        return 2;
    }
    if (gpio_get_level(KEY_DOWN_PIN) == 0)
    {
        printf("key down pressed\n");
        return 1;
    }
    if (gpio_get_level(KEY_CENTER_PIN) == 0)
    {
        if (!st7789_lcd_backlight_get())
        {
            while (gpio_get_level(KEY_CENTER_PIN) == 0);
            st7789_lcd_backlight_set(true);
            lcd_off_timer_start();
            return 0;
        }
        printf("key center pressed\n");
        return 5;
    }
    return 0;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
