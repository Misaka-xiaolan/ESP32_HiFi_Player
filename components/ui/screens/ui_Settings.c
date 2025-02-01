//
// Created by Misaka on 24-12-26.
//
#include <stdio.h>
#include <esp_a2dp_api.h>
#include "../ui.h"
#include "bluetooth.h"
#include "freertos_task.h"
#include "audioplay.h"
#include "bt_app_core.h"

//static lv_obj_t *bluetooth_search;
static lv_obj_t *pannel;
extern lv_indev_t *indev_keypad;

lv_obj_t *back_btn;
lv_obj_t *btn1_label;
lv_obj_t *btn2;
lv_obj_t *btn3;
lv_obj_t *btn4;

static void Back_Btn_Event(lv_event_t *e);

static void Bluetooth_Device_Connect_Btn_Event(lv_event_t *event);

void Bluetooth_Search_Cplt_Callback(void)
{
    uint16_t total_bt_device = Get_bt_device_num();
    char label_str[30] = {0};
    for (uint16_t i = 0; i < total_bt_device; i++)
    {
        lv_obj_t *btn = lv_btn_create(pannel);
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_width(btn, LV_SIZE_CONTENT);
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(label, &lv_font_cn_14, 0);
        sprintf(label_str, "%d:%s", i + 1, bt_device_list[i].d_name);
        lv_label_set_text(label, label_str);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lv_group_add_obj(group, btn);
        lv_obj_add_event_cb(btn, Bluetooth_Device_Connect_Btn_Event, LV_EVENT_CLICKED, (void *) (i + 1));
    }
    Set_bt_device_num(0);
    lv_obj_set_style_text_font(btn1_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(btn1_label, "< Back");
    lv_obj_add_event_cb(back_btn, Back_Btn_Event, LV_EVENT_RELEASED, NULL);
}

void Bluetooth_Connected_Callback(void) //需保证线程安全
{
    memset(&bt_device_list, 0, BT_LIST_LEN * sizeof(BT_DEVICE_LIST));
    if (display_num == 1)
    {
        xSemaphoreTake(MutexSemaphore, portMAX_DELAY);
        lv_group_remove_all_objs(group);
        Menu_Load();
        bluetooth_play = 1;
        xSemaphoreGive(MutexSemaphore); /* 释放互斥信号量 */
        Lv_Widgets_Del_All(settings);
    }
}

static void Back_Btn_Event(lv_event_t *e)
{
    bt_app_task_shut_down();
    lv_group_remove_all_objs(group);

//    xSemaphoreTake(MutexSemaphore,portMAX_DELAY);
//    lv_obj_clean(settings);
    Lv_Widgets_Del_All(settings);
    Menu_Load();
//    xSemaphoreGive(MutexSemaphore);
}

static void Bluetooth_Device_Connect_Btn_Event(lv_event_t *e)
{
    uint16_t btn_num;
    btn_num = (uint16_t) lv_event_get_user_data(e) - 1;
    printf("btn_num:%d\n", btn_num);
    Set_s_peer_bda(bt_device_list[btn_num].d_bda);
    strcpy(target_device_name, (char *) bt_device_list[btn_num].d_name);
    printf("name:%s\n\n", target_device_name);
//    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 5, 0);
    Set_A2DP_Global_State(APP_AV_STATE_CONNECTING);
    printf("err:%d", esp_a2d_source_connect(bt_device_list[btn_num].d_bda));
//    Bluetooth_Connect();
    lv_obj_remove_event_cb(back_btn, Back_Btn_Event);
    lv_obj_set_style_text_font(btn1_label, &lv_font_cn_14, 0);
    lv_label_set_text(btn1_label, "连接中...");
}

void Settings_Menu_Load(void)
{
    lv_obj_clear_flag(settings, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(title, "  Settings");

    lv_group_add_obj(group, back_btn);
    display_num = 1;
}

void Settings_Menu_Init(void)
{
    settings = lv_obj_create(main_screen);
    lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(settings, 280, 175);
    lv_obj_clear_flag(settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(settings, 0, 75);
    lv_obj_set_style_radius(settings, 45, 0);
    lv_obj_set_style_shadow_color(settings, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_width(settings, 20, 0);
    lv_obj_set_style_shadow_ofs_y(settings, -10, 0);

    pannel = lv_obj_create(settings);
    lv_obj_set_size(pannel, 250, 150);
    lv_obj_center(pannel);
    lv_obj_set_style_border_width(pannel, 0, 0);
//    lv_obj_remove_style(pannel, 0, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(pannel, LV_FLEX_FLOW_COLUMN); // 垂直排列子对象
    lv_obj_set_scroll_dir(pannel, LV_DIR_VER);

    back_btn = lv_btn_create(pannel);
    lv_obj_set_width(back_btn, LV_SIZE_CONTENT);
    lv_obj_set_height(back_btn, LV_SIZE_CONTENT);
    lv_obj_align(back_btn, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    btn1_label = lv_label_create(back_btn);
    lv_obj_align(btn1_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(btn1_label, "请等待蓝牙搜索完成");
    lv_obj_set_style_text_font(btn1_label, &lv_font_cn_14, 0);

//    btn2 = lv_btn_create(pannel);
//    lv_obj_set_width(btn2, LV_SIZE_CONTENT);
//    lv_obj_set_height(btn2, LV_SIZE_CONTENT);
//    lv_obj_align(btn2, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
//    lv_obj_t *btn2_label = lv_label_create(btn2);
//    lv_obj_align(btn2_label, LV_ALIGN_CENTER, 0, 0);
//    lv_label_set_text(btn2_label, "你好 こんにちは hello");
//    lv_obj_set_style_text_font(btn2_label, &lv_font_cn_14, 0);

//    btn3 = lv_btn_create(pannel);
//    lv_obj_set_width(btn3, 50);
//    lv_obj_set_height(btn3, 20);
//    lv_obj_align(btn3, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
//    lv_obj_t *btn3_label = lv_label_create(btn3);
//    lv_obj_align(btn3_label, LV_ALIGN_CENTER, 0, 0);
//    lv_label_set_text(btn3_label, "Pin Map3");
//    lv_obj_set_style_text_font(btn3_label, &lv_font_montserrat_20, 0);

//    btn4 = lv_btn_create(pannel);
//    lv_obj_set_width(btn4, 50);
//    lv_obj_set_height(btn4, 20);
//    lv_obj_align(btn4, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
//    lv_obj_t *btn4_label = lv_label_create(btn4);
//    lv_obj_align(btn4_label, LV_ALIGN_CENTER, 0, 0);
//    lv_label_set_text(btn4_label, "Pin Map4");
//    lv_obj_set_style_text_font(btn4_label, &lv_font_montserrat_20, 0);

}

