//
// Created by Misaka on 25-1-16.
//
#include <stdio.h>
#include <esp_gap_bt_api.h>
#include <dirent.h>
#include "../ui.h"
#include "freertos_task.h"
#include "audioplay.h"

static lv_obj_t *music_table;

void List_Btn_Event_Callback(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t col = 0;
    uint16_t row = 0;
    lv_table_get_selected_cell(obj, &row, &col);
    printf("选择了第%hu列，第%hu行\n", col, row);
    lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    Audio_Play_Indexed(row);
    Play_Menu_Load();
}

void List_Add_Music(void)
{
    uint16_t music_total = audiodev.mfilenum[audiodev.dir_num];
    struct dirent *fileinfo;
    DIR *audiodir = NULL;
    audiodir = opendir((const char *) audiodev.path);    //打开目录
    if (audiodir == NULL) return;
    lv_table_set_row_cnt(music_table, music_total);
    for (uint16_t i = 0; i < music_total; i++)
    {
        seekdir(audiodir, audiodev.mfindextbl[audiodev.dir_num][i]); //改变当前目录索引
        fileinfo = readdir(audiodir);            //读取目录下的一个文件
        if (fileinfo == NULL)
        {
            closedir(audiodir);
            printf("打开失败\n");
            return;//打开失败
        }
        lv_table_set_cell_value(music_table, i, 0, fileinfo->d_name);
    }
    closedir(audiodir);
}

void List_Menu_Init(void)
{
    list = lv_obj_create(main_screen);
    lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(list, 280, 175);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(list, 0, 75);
    lv_obj_set_style_radius(list, 45, 0);
    lv_obj_set_style_shadow_color(list, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_width(list, 20, 0);
    lv_obj_set_style_shadow_ofs_y(list, -10, 0);

    music_table = lv_table_create(list);
    lv_obj_set_size(music_table, 250, 150);
    lv_obj_center(music_table);
    lv_obj_set_style_border_width(music_table, 0, 0);
    lv_obj_set_style_text_font(music_table, &lv_font_cn_14, 0);
    lv_table_set_col_width(music_table, 0, 250);
    lv_table_set_col_cnt(music_table, 1);
    lv_obj_add_event_cb(music_table, List_Btn_Event_Callback, LV_EVENT_RELEASED, 0);
    lv_obj_set_style_anim_time(music_table, 0, 0);
    lv_anim_del(music_table, NULL);
}

void List_Menu_Load(void)
{
    lv_group_remove_all_objs(group);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(group, music_table);
}
