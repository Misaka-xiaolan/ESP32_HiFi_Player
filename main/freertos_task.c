//
// Created by Misaka on 24-10-18.
//

#include "freertos_task.h"
#include "esp_log.h"
#include <dirent.h>   //定义了用于遍历目录结构的类型和函数
#include <sys/stat.h> //提供了用于获取文件或目录状态信息的接口
#include <string.h>
#include "sdcard.h"
#include "audioplay.h"
#include "mp3play.h"
#include "flacplay.h"
#include "lv_api_map.h"
#include "st7789_driver.h"
#include "lv_port.h"
#include "ui.h"

TaskHandle_t gui_display_task_handler;
TaskHandle_t audio_play_task_handler;
TaskHandle_t lv_widgets_delete_task_handler;


SemaphoreHandle_t semphore_handle;
SemaphoreHandle_t bluetooth_send_semphore;
SemaphoreHandle_t audio_task_create_semphore;
SemaphoreHandle_t MutexSemaphore;

QueueHandle_t lv_widgets_delete_queue;

void Audio_Play_Task(void *pvParameters)
{
    printf("音乐播放任务启动\n");
    DIR *audiodir = NULL; //audiodir专用
    struct dirent *audioinfo;
    uint8_t *fname = NULL;//临时文件名+路径名指针
    char key[16];
    volatile int32_t *curindex;
    while (audiodev.status & 0x80)//只要还在播放
    {
        xSemaphoreGive(audio_task_create_semphore);
        xTaskNotifyWaitIndexed(1, 0, 0xFFFFFFFF, (uint32_t *) &audiodev.dir_num,
                               portMAX_DELAY); //接收Audio_Play函数或GUI操作发送来的目录号
        if (audiodev.dir_num > MAX_DIR_NUM - 1) audiodev.dir_num = 0; //判断索引是否越界
        else if (audiodev.dir_num < 0) audiodev.dir_num = MAX_DIR_NUM - 1; //判断索引是否越界
        curindex = &audiodev.curindex[audiodev.dir_num];
        xTaskNotifyWaitIndexed(0, 0, 0xFFFFFFFF, (uint32_t *) curindex, portMAX_DELAY); //接收Audio_Play函数或GUI操作发送来的文件索引

        if (*curindex >= audiodev.mfilenum[audiodev.dir_num]) *curindex = 0; //当执行完“下一曲”操作，判断索引是否越界
        if (*curindex < 0) *curindex = audiodev.mfilenum[audiodev.dir_num] - 1; //当执行完“上一曲”操作，判断索引是否越界
        sprintf(key, "music_idx_%ld", audiodev.dir_num);
        nvs_set_i32(audioplayer_storage, key, *curindex); //将当前的索引值存入NVS
        nvs_set_i32(audioplayer_storage, "dir_idx", audiodev.dir_num); //将当前的目录号存入NVS
        nvs_commit(audioplayer_storage);
        sprintf((char *) audiodev.path, "/sdcard/List%ld", audiodev.dir_num + 1);
        audiodir = opendir((const char *) audiodev.path);    //打开目录
        while (audiodir != NULL)//打开成功
        {
            if (audiodev.mfilenum[audiodev.dir_num] == 0) //该目录下没有音乐文件
            {
                audiodev.name = (uint8_t *) "没有音乐文件";
                Ui_Update_Music_Infos(0);
                break;
            }
            audiodev.status |= (1 << 1);
            nvs_set_i32(audioplayer_storage, key, *curindex); //将当前的索引值存入NVS
            seekdir(audiodir, audiodev.mfindextbl[audiodev.dir_num][*curindex]); //改变当前目录索引
            audioinfo = readdir(audiodir);            //读取目录下的一个文件
            if (audioinfo == NULL) break;//打开失败
            audiodev.name = (uint8_t *) (audioinfo->d_name);//获取文件名，长文件名/短文件名
            fname = malloc(strlen((const char *) audiodev.name) + strlen((const char *) audiodev.path) + 2);//为总文件名申请内存
            fname = gui_path_name(fname, audiodev.path, audiodev.name);    //文件名加入路径
            audiodev.status |= 1 << 4;//标记正在播放音乐
            audiodev.status |= 1 << 5;//标记切歌了
            Ui_Update_Music_Infos(*curindex);
            switch (f_typetell((char *) fname))
            {
                case T_MP3:
                    printf("播放MP3\n");
                    MP3_Play_Song(fname, 0);    //播放MP3文件
                    break;
                case T_FLAC:
                    printf("播放FLAC\n");
                    Flac_Play_Song(fname, 0);    //播放flac文件
                    break;
                default:
                    break;
            }
            //用完总文件名
            free(fname);//释放内存
            fname = NULL;
            if ((audiodev.status & (1 << 6)) == 0)//不终止播放
            {
                if (xTaskNotifyWaitIndexed(2, 0, 0xFFFFFFFF, NULL, 0) == pdTRUE) //如果GUI中有操作(读取1处的通知并清零)
                {
                    break;//退出循环，等待新的索引
                }
                if (audiodev.mode == 1)//列表顺序播放
                {
                    if (*curindex < (audiodev.mfilenum[audiodev.dir_num] - 1)) (*curindex)++;
                    else
                    {
                        *curindex = audiodev.mfilenum[audiodev.dir_num] - 1;
                        audiodev.status &= ~(1 << 0);
                        break;//退出自动循环，停留在邮箱等待索引那里
                    }
                }
                else if (audiodev.mode == 2)//单曲循环播放
                {
                    *curindex = *curindex;//单曲循环
                }
                else if (audiodev.mode == 0)//列表循环播放
                {
                    if (*curindex < (audiodev.mfilenum[audiodev.dir_num] - 1)) (*curindex)++;
                    else *curindex = 0;
                }
//				else if(audiodev.mode == 3)//随机播放
//				{
//					audiodev.curindex=RNG_Get_RandomRange(0,audiodev.mfilenum);//得到下一首歌曲的索引
//				}
            }
            else break;
        }
    }
    closedir(audiodir);//关闭目录
    audiodev.status &= ~(1 << 6);    //标记已经成功终止播放
    audiodev.status &= ~(1 << 4);    //标记无音乐播放
    for (int i = 0; i < MAX_DIR_NUM - 1; ++i)
    {
        free(audiodev.mfindextbl[i]);
        audiodev.mfindextbl[i] = NULL;
    }
    vTaskDelete(NULL);
}


void Gui_Display_Task(void *fuck)
{
    lv_Port_Init();
    st7789_lcd_backlight_set(1);
    xSemaphoreTake(MutexSemaphore, portMAX_DELAY);
    ui_init();
    Main_Menu_Init();
    Info_Menu_Init();
    xSemaphoreGive(MutexSemaphore);
    while (1)
    {
        xSemaphoreTake(MutexSemaphore, portMAX_DELAY);
        lv_task_handler();
        xSemaphoreGive(MutexSemaphore); /* 释放互斥信号量 */
        vTaskDelay(30);
    }
}

void Lv_Widgets_Delete_Task(void *pvParameters)
{
    lv_obj_t *p;
    while (1)
    {
        xQueueReceive(lv_widgets_delete_queue, &p, portMAX_DELAY);
        xSemaphoreTake(MutexSemaphore, portMAX_DELAY);
        lv_obj_del(p);
        xSemaphoreGive(MutexSemaphore); /* 释放互斥信号量 */
    }
}




