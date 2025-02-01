//
// Created by Misaka on 25-1-15.
//

#include <stdio.h>
#include <esp_gap_bt_api.h>
#include "../ui.h"
#include "freertos_task.h"
#include "audioplay.h"
#include "cs4398.h"

static lv_obj_t *volume_slider;
static lv_obj_t *music_name;
static lv_obj_t *folder_name;
static lv_obj_t *current_time;
static lv_obj_t *total_time;
static lv_obj_t *music_number;
static lv_obj_t *music_class;
static lv_obj_t *music_bitdepth;
static lv_obj_t *music_samplerate;
static lv_obj_t *bar;
static lv_obj_t *play_btn;
static lv_obj_t *next_btn;
static lv_obj_t *last_btn;
static lv_obj_t *list_btn;
static lv_obj_t *mode_btn;
static lv_obj_t *folder_next_btn;
static lv_obj_t *folder_last_btn;

static lv_obj_t *play_btn_icon;
static lv_obj_t *next_btn_icon;
static lv_obj_t *last_btn_icon;
static lv_obj_t *list_btn_icon;
static lv_obj_t *mode_btn_icon;
static lv_obj_t *volume_icon;
static lv_obj_t *folder_next_btn_icon;
static lv_obj_t *folder_last_btn_icon;

lv_timer_t *info_update_timer;

void Ui_Update_Music_Infos(int32_t f_idx) //外部调用更新音乐名  需保证线程安全
{
    xSemaphoreTake(MutexSemaphore, portMAX_DELAY);
    lv_label_set_text(music_name, (char *) audiodev.name);
    lv_label_set_text_fmt(music_number, "%03ld / %03d", f_idx + 1, audiodev.mfilenum[audiodev.dir_num]);
    lv_label_set_text_fmt(folder_name, "List%ld", audiodev.dir_num + 1);
    xSemaphoreGive(MutexSemaphore); /* 释放互斥信号量 */
}

static void Info_Update_Callback(lv_timer_t *timer)
{
    lv_label_set_text_fmt(total_time, "%02ld:%02ld", audiodev.totsec / 60, audiodev.totsec % 60);
    lv_label_set_text_fmt(current_time, "%02ld:%02ld", audiodev.cursec / 60, audiodev.cursec % 60);
    lv_label_set_text_fmt(music_samplerate, "%ldHz", audiodev.samplerate);
    if (audiodev.samplerate >= 96000)
    {
        lv_label_set_long_mode(music_name, LV_LABEL_LONG_DOT);
    }
    else
    {
        lv_label_set_long_mode(music_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    switch (audiodev.file_type)
    {
        case FILE_FLAC_16Bit:
            lv_label_set_text(music_bitdepth, "16bit");
            lv_label_set_text(music_class, "FLAC");
            break;
        case FILE_FLAC_24Bit:
            lv_label_set_text(music_class, "FLAC");
            lv_label_set_text(music_bitdepth, "24bit");
            break;
        case FILE_MP3:
            lv_label_set_text(music_class, "MP3");
            lv_label_set_text(music_bitdepth, "16bit");
            break;
        default:
            lv_label_set_text(music_class, "NONE");
            break;
    }

    lv_bar_set_value(bar, (int32_t) (100 * (float) audiodev.cursec / (float) audiodev.totsec), LV_ANIM_OFF);
}

static void Volume_Slider_Event_Callback(lv_event_t *e)
{
    int32_t volume = lv_slider_get_value(volume_slider);
    audiodev.vol = volume;
    nvs_set_i32(audioplayer_storage, "volume", audiodev.vol);
    nvs_commit(audioplayer_storage);
    CS4398_HPvol_Set(audiodev.vol);
}

static void Play_Btn_Event_Callback(lv_event_t *e)
{
    if ((audiodev.status & 0x01) == 1)
    {
        lv_img_set_src(play_btn_icon, LV_SYMBOL_PAUSE);
        audiodev.status &= ~0x01;
    }
    else
    {
        lv_img_set_src(play_btn_icon, LV_SYMBOL_PLAY);
        audiodev.status |= 0x01;
    }
}

static void Next_Btn_Event_Callback(lv_event_t *e)
{
    Audio_Play_Next();
}

static void Last_Btn_Event_Callback(lv_event_t *e)
{
    Audio_Play_Last();
}

static void Mode_Btn_Event_Callback(lv_event_t *e)
{
    if (audiodev.mode == 0)
    {
        lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0xeeb54e), 0);
        audiodev.mode = 2;
    }
    else
    {
        lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0xe0e4e3), 0);
        audiodev.mode = 0;
    }
}

static void List_Btn_Event_Callback(lv_event_t *e)
{
    lv_timer_pause(info_update_timer);
    List_Add_Music();
    List_Menu_Load();
}

static void Folder_Next_Btn_Event_Callback(lv_event_t *e)
{
    Audio_Play_Next_Folder();
}

static void Folder_Last_Btn_Event_Callback(lv_event_t *e)
{
    Audio_Play_Last_Folder();
}

void Play_Menu_Init(void)
{
    static lv_anim_t animation_template;
    static lv_style_t label_style;

    lv_anim_init(&animation_template);
    lv_anim_set_delay(&animation_template, 1000);           /*Wait 1 second to start the first scroll*/
    lv_anim_set_repeat_delay(&animation_template, 3000);
    lv_style_init(&label_style);
    lv_style_set_anim(&label_style, &animation_template);

    play = lv_obj_create(main_screen);
    lv_obj_add_flag(play, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(play, 280, 175);
    lv_obj_clear_flag(play, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(play, 0, 75);
    lv_obj_set_style_radius(play, 45, 0);
    lv_obj_set_style_shadow_color(play, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_width(play, 20, 0);
    lv_obj_set_style_shadow_ofs_y(play, -10, 0);

    volume_slider = lv_slider_create(play);

    music_name = lv_label_create(play);
    if (bluetooth_play)
    {
        lv_label_set_long_mode(music_name, LV_LABEL_LONG_DOT);
    }
    else
    {
        lv_label_set_long_mode(music_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    lv_obj_set_style_text_font(music_name, &lv_font_cn_14, 0);
    lv_obj_add_style(music_name, &label_style, LV_STATE_DEFAULT);

    current_time = lv_label_create(play);
    lv_obj_set_style_text_font(current_time, &lv_font_montserrat_12, 0);

    total_time = lv_label_create(play);
    lv_obj_set_style_text_font(total_time, &lv_font_montserrat_12, 0);

    music_number = lv_label_create(play);
    music_class = lv_label_create(play);
    music_bitdepth = lv_label_create(play);
    music_samplerate = lv_label_create(play);
    folder_name = lv_label_create(play);
    lv_obj_set_style_text_font(music_number, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(folder_name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(music_class, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(music_bitdepth, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(music_samplerate, &lv_font_montserrat_12, 0);

    bar = lv_bar_create(play);
    play_btn = lv_btn_create(play);
    next_btn = lv_btn_create(play);
    last_btn = lv_btn_create(play);
    list_btn = lv_btn_create(play);
    mode_btn = lv_btn_create(play);
    folder_next_btn = lv_btn_create(play);
    folder_last_btn = lv_btn_create(play);

    play_btn_icon = lv_img_create(play_btn);
    next_btn_icon = lv_img_create(next_btn);
    last_btn_icon = lv_img_create(last_btn);
    list_btn_icon = lv_img_create(list_btn);
    mode_btn_icon = lv_img_create(mode_btn);
    volume_icon = lv_img_create(play);
    folder_next_btn_icon = lv_img_create(folder_next_btn);
    folder_last_btn_icon = lv_img_create(folder_last_btn);
    lv_img_set_src(play_btn_icon, LV_SYMBOL_PLAY);
    lv_img_set_src(next_btn_icon, LV_SYMBOL_NEXT);
    lv_img_set_src(last_btn_icon, LV_SYMBOL_PREV);
    lv_img_set_src(list_btn_icon, LV_SYMBOL_BARS);
    lv_img_set_src(mode_btn_icon, LV_SYMBOL_LOOP);
    lv_img_set_src(folder_next_btn_icon, LV_SYMBOL_RIGHT);
    lv_img_set_src(folder_last_btn_icon, LV_SYMBOL_LEFT);
    lv_img_set_src(volume_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_center(play_btn_icon);
    lv_obj_center(next_btn_icon);
    lv_obj_center(last_btn_icon);
    lv_obj_center(list_btn_icon);
    lv_obj_center(mode_btn_icon);
    lv_obj_center(folder_next_btn_icon);
    lv_obj_center(folder_last_btn_icon);


    lv_obj_set_size(volume_slider, 85, 5);
    lv_obj_set_size(music_name, 185, 22);
    lv_obj_set_size(music_number, 63, LV_SIZE_CONTENT);
    lv_obj_set_size(music_class, 46, LV_SIZE_CONTENT);
    lv_obj_set_size(music_bitdepth, 40, LV_SIZE_CONTENT);
    lv_obj_set_size(music_samplerate, 88, LV_SIZE_CONTENT);
    lv_obj_set_size(folder_name, 40, LV_SIZE_CONTENT);
    lv_obj_set_size(current_time, 61, 16);
    lv_obj_set_size(total_time, 61, 16);
    lv_obj_set_size(bar, 226, 7);
    lv_obj_set_size(play_btn, 35, 35);
    lv_obj_set_size(next_btn, 35, 35);
    lv_obj_set_size(last_btn, 35, 35);
    lv_obj_set_size(list_btn, 35, 35);
    lv_obj_set_size(mode_btn, 35, 35);
    lv_obj_set_size(folder_next_btn, 27, 14);
    lv_obj_set_size(folder_last_btn, 27, 14);
    lv_obj_set_size(volume_icon, 18, 16);
    lv_bar_set_range(volume_slider, 0, 100);

    lv_obj_set_style_radius(play_btn, 40, 0);
    lv_obj_set_style_radius(next_btn, 40, 0);
    lv_obj_set_style_radius(last_btn, 40, 0);
    lv_obj_set_style_radius(list_btn, 40, 0);
    lv_obj_set_style_radius(mode_btn, 40, 0);
    lv_obj_set_style_text_align(total_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(music_number, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(folder_name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(music_class, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(music_bitdepth, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(music_samplerate, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x33d0b3), 0);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0xeeb54e), 0);
    lv_obj_set_style_bg_color(last_btn, lv_color_hex(0xeeb54e), 0);
    lv_obj_set_style_bg_color(list_btn, lv_color_hex(0xeeb54e), 0);
    lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0xe0e4e3), 0);


    lv_obj_set_pos(volume_slider, 167 - 15, 93 - 85);
    lv_obj_set_pos(music_name, 27 - 15, 120 - 85);
    lv_obj_set_pos(current_time, 27 - 15, 146 - 85);
    lv_obj_set_pos(total_time, 194 - 15, 146 - 85);
    lv_obj_set_pos(bar, 27 - 15, 166 - 85);
    lv_obj_set_pos(play_btn, 122 - 15, 185 - 85);
    lv_obj_set_pos(next_btn, 167 - 15, 185 - 85);
    lv_obj_set_pos(last_btn, 74 - 15, 185 - 85);
    lv_obj_set_pos(list_btn, 213 - 15, 185 - 85);
    lv_obj_set_pos(mode_btn, 29 - 15, 185 - 85);
    lv_obj_set_pos(folder_next_btn, 104 - 15, 87 - 85);
    lv_obj_set_pos(folder_last_btn, 74 - 15, 87 - 85);
    lv_obj_set_pos(music_number, 27 - 15, 102 - 85);
    lv_obj_set_pos(folder_name, 27 - 15, 87 - 85);
    lv_obj_set_pos(music_class, 210 - 15, 118 - 85);
    lv_obj_set_pos(music_bitdepth, 216 - 15, 131 - 85);
    lv_obj_set_pos(music_samplerate, 98 - 15, 146 - 85);
    lv_obj_set_pos(volume_icon, 142 - 15, 87 - 85);

    lv_obj_add_event_cb(volume_slider, Volume_Slider_Event_Callback, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(play_btn, Play_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(next_btn, Next_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(last_btn, Last_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mode_btn, Mode_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(list_btn, List_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(folder_next_btn, Folder_Next_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(folder_last_btn, Folder_Last_Btn_Event_Callback, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(music_name, "这是一个音乐文件");
    lv_label_set_text(current_time, "01:26");
    lv_label_set_text(total_time, "04:14");
    lv_label_set_text(folder_name, "List1");

    info_update_timer = lv_timer_create(Info_Update_Callback, 500, NULL);
    lv_timer_ready(info_update_timer);
}

void Play_Menu_Load(void)
{
    lv_obj_clear_flag(play, LV_OBJ_FLAG_HIDDEN);
    lv_timer_resume(info_update_timer);
    lv_group_add_obj(group, folder_last_btn);
    lv_group_add_obj(group, folder_next_btn);
    lv_group_add_obj(group, volume_slider);
    lv_group_add_obj(group, mode_btn);
    lv_group_add_obj(group, last_btn);
    lv_group_add_obj(group, play_btn);
    lv_group_add_obj(group, next_btn);
    lv_group_add_obj(group, list_btn);
    lv_slider_set_value(volume_slider, audiodev.vol, LV_ANIM_OFF);
    lv_group_focus_obj(play_btn);
}