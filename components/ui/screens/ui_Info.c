//
// Created by Misaka on 25-2-9.
//

#include "../ui.h"

lv_obj_t *info_back_btn;
lv_obj_t *back_btn_img;
lv_obj_t *text;

void info_back_btn_Callback(lv_event_t *e)
{
    lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN);
    lv_group_remove_all_objs(group);
    Menu_Load();
}

void Info_Menu_Init(void)
{
    info = lv_obj_create(main_screen);
    lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(info, 280, 175);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(info, 0, 75);
    lv_obj_set_style_radius(info, 45, 0);
    lv_obj_set_style_shadow_color(info, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_width(info, 20, 0);
    lv_obj_set_style_shadow_ofs_y(info, -10, 0);

    info_back_btn = lv_btn_create(info);
    lv_obj_add_event_cb(info_back_btn, info_back_btn_Callback, LV_EVENT_CLICKED, NULL);
    back_btn_img = lv_img_create(info_back_btn);
    lv_img_set_src(back_btn_img, LV_SYMBOL_LEFT);
    lv_obj_set_size(info_back_btn, 27, 14);
    lv_obj_set_pos(info_back_btn, 29 - 15, 87 - 85);
    text = lv_label_create(info);
    lv_obj_set_size(text, 224, 88);
    lv_obj_set_pos(text, 29 - 15, 118 - 85);
    lv_obj_set_style_text_font(text, &lv_font_cn_14, 0);
    lv_label_set_text_fmt(text, "ESP32 HiFi播放器\n\n作者:B站 御坂晓岚\n\n固件编译日期:2025/02/09\n\nMisaka_xiaolan");
}

void Info_Menu_Load(void)
{
    lv_obj_clear_flag(info, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(group, info_back_btn);
    lv_label_set_text(title, "System Info");
}

