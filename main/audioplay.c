//
// Created by Misaka on 24-10-18.
//

#include <string.h>
#include "audioplay.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <dirent.h>   //定义了用于遍历目录结构的类型和函数
#include <sys/stat.h> //提供了用于获取文件或目录状态信息的接口
#include <nvs_flash.h>
#include "sdcard.h"
#include "freertos_task.h"
#include "cs4398.h"

volatile __audiodev audiodev = {0}; //音乐文件控制器全局变量
volatile uint8_t bluetooth_play = 0;

nvs_handle_t audioplayer_storage;

uint8_t *gui_path_name(uint8_t *pname, uint8_t *path, uint8_t *name)
{
    const uint8_t chgchar[2] = {0X2F, 0X00};//转义符 等效"/"
    strcpy((char *) pname, (const char *) path);    //拷贝path到pname里面
    strcat((char *) pname, (const char *) chgchar);    //添加转义符
    strcat((char *) pname, (const char *) name);        //添加新增的名字
    return pname;
}

//查询目录下总音频文件个数
//path:目录路径
uint16_t Audio_Get_Total_Num(char *path)
{
    uint16_t tnum = 0;
    uint8_t res = 0;
    DIR *tdir = NULL;            //临时目录
    struct dirent *dirent;
    char *fn;    //文件名+路径名指针
    tdir = opendir(path);
    if (tdir != NULL)
    {
        while (1)
        {
            dirent = readdir(tdir);
            if (dirent == NULL) break;
            fn = (char *) &(dirent->d_name);
            res = f_typetell(fn);
            if ((res & 0xF0) == 0x40)   /* 取高四位,看看是不是音乐文件 */
            {
                tnum++; /* 有效文件数增加1 */
            }
        }
    }
    closedir(tdir);
    return tnum;
}

static uint8_t Audio_Play_Task_Create(void)
{
    uint8_t res = 0;
    vTaskSuspendAll();
    res = xTaskCreatePinnedToCore(Audio_Play_Task, "audio_play", AUDIO_PLAY_STACK_SIZE, NULL, AUDIO_PLAY_PRIO,
                                  &audio_play_task_handler, 1);
    xTaskResumeAll();
    if (res != pdTRUE)
    {
        printf("音乐播放任务创建失败!\n");
        return 1;
    }

    audiodev.status |= 1 << 7;

    return 0;
}

void Audio_Play_Task_Delete(void)
{
    audiodev.status &= ~(1 << 7);
    vTaskDelete(audio_play_task_handler);
    for (int i = 0; i < MAX_DIR_NUM - 1; ++i)
    {
        free(audiodev.mfindextbl[i]);
        audiodev.mfindextbl[i] = NULL;
    }
}

uint8_t Audio_Play(void)
{
    uint32_t temp; //临时索引值
    char *fn; //文件名
    uint8_t res = 0;
    DIR *audiodir;            //目录
    struct dirent *audiofileinfo = NULL;//文件信息
    uint16_t totalMusicNum;
    esp_err_t err;
    audiodev.vol = 35;

    err = nvs_open("storage", NVS_READWRITE, &audioplayer_storage); //打开NVS存储分区
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }

    err = nvs_get_i32(audioplayer_storage, "volume", (int32_t *) &audiodev.vol); //读取存储的音量值
    switch (err)
    {
        case ESP_OK:
            printf("Volume storaged = %" PRIu32 "\n", audiodev.vol);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            nvs_set_i32(audioplayer_storage, "volume", audiodev.vol);
            nvs_commit(audioplayer_storage);
            break;
        default :
            printf("Error (%s) reading!\n", esp_err_to_name(err));
    }
    if ((audiodev.status & (1 << 7)) == 0)    //音频播放任务已经删除/第一次进入
    {
        CS4398_HPvol_Set(audiodev.vol);
        xSemaphoreTake(audio_task_create_semphore, portMAX_DELAY);
        memset(&audiodev, 0, sizeof(__audiodev));//audiodev所有数据清零.
        audiodev.status |= (1 << 0);
        res = Audio_Play_Task_Create();        //创建任务
        xSemaphoreTake(audio_task_create_semphore, portMAX_DELAY);
        if (res) return res;
        //任务创建成功才会申请内存
        audiodev.path = malloc(FF_MAX_LFN * 2 + 1);
        if (audiodev.path == NULL)
        {
            return 1;
        }
        for (uint8_t i = 0; i < MAX_DIR_NUM; i++) //对六个列表目录进行遍历，查询每个目录的音乐总数，并储存
        {
            sprintf((char *) audiodev.path, "/sdcard/List%d", i + 1);
            audiodir = opendir((const char *) audiodev.path);
            if (audiodir == NULL)
            {
                printf("打开目录失败，将创建目录\n");
                mkdir((char *) audiodev.path, 0777);
                audiodir = opendir((const char *) audiodev.path);
                if (audiodir == NULL)
                {
                    printf("创建目录失败！\n");
                    //释放路径内存
                    free(audiodev.path);
                    audiodev.path = NULL;
                    return 1;//退出
                }
            }
            totalMusicNum = Audio_Get_Total_Num((char *) audiodev.path);
            if (totalMusicNum == 0)//音乐文件总数为0
            {
                printf("List%d没有音频文件!\n", i + 1);
                audiodev.mfilenum[i] = 0;
                audiodev.mfindextbl[i] = (uint32_t *) malloc(4 * 2);    //申请一个小内存，防止空指针解引用
                continue;
            }
            audiodev.mfindextbl[i] = (uint32_t *) malloc(4 * totalMusicNum);    //申请4*totwavnum个字节的内存,用于存放音乐文件索引
            if (audiodev.mfindextbl[i] == NULL)//内存分配出错
            {
                printf("申请文件索引时内存不足!\n");
                //释放内存
                free(audiodev.path);
                audiodev.path = NULL;
                return 1;//退出
            }
            audiodev.curindex[i] = 0;//当前索引为0
            while (1)//全部查询一遍
            {
                temp = telldir(audiodir);                                //记录当前index
                audiofileinfo = readdir(audiodir); //读取目录下的一个文件
                if (audiofileinfo == NULL) break;    //错误了/到末尾了,退出
                fn = (char *) &(audiofileinfo->d_name);
                res = f_typetell(fn);//判断文件类型
                if ((res & 0XF0) == 0X40)//取高四位,看看是不是音乐文件
                {
                    audiodev.mfindextbl[i][audiodev.curindex[i]] = temp;//记录索引
                    audiodev.curindex[i]++;
                }
            }
            audiodev.mfilenum[i] = audiodev.curindex[i];//保存总索引数目
            audiodev.curindex[i] = 0;
            char key[16] = {0};
            sprintf(key, "music_idx_%d", i);
            err = nvs_get_i32(audioplayer_storage, key, (int32_t *) &audiodev.curindex[i]); //读取存储的索引值
            switch (err)
            {
                case ESP_OK:
                    printf("music_idx_%d storaged = %" PRIu32 "\n", i + 1, audiodev.curindex[i]);
                    break;
                case ESP_ERR_NVS_NOT_FOUND:
                    nvs_set_i32(audioplayer_storage, key, audiodev.curindex[i]);
                    nvs_commit(audioplayer_storage);
                    break;
                default :
                    printf("Error (%s) reading!\n", esp_err_to_name(err));
            }
            if (audiodev.curindex[i] > audiodev.mfilenum[i] - 1) audiodev.curindex[i] = 0;//从0开始显示
            printf("List%d音乐总数为: %d\n", i + 1, audiodev.mfilenum[i]);
            closedir(audiodir);
        }
        audiodev.dir_num = 0;
        err = nvs_get_i32(audioplayer_storage, "dir_idx", (int32_t *) &audiodev.dir_num); //读取存储的索引值
        switch (err)
        {
            case ESP_OK:
                printf("dir_idx storaged = %" PRIu32 "\n", audiodev.dir_num);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                nvs_set_i32(audioplayer_storage, "dir_idx", audiodev.dir_num);
                nvs_commit(audioplayer_storage);
                break;
            default :
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
        xTaskNotifyIndexed(audio_play_task_handler, 0, audiodev.curindex[audiodev.dir_num], eSetValueWithOverwrite);
        xTaskNotifyIndexed(audio_play_task_handler, 1, audiodev.dir_num, eSetValueWithOverwrite);
    }
    else
    {
        printf("错误:音乐播放任务已启动\n");
    }
    return 0;
}

void Audio_Play_Next(void)
{
    xTaskNotifyIndexed(audio_play_task_handler, 2, 0XFF, eSetValueWithOverwrite);//向音乐播放任务发送通知，通知GUI有操作
    audiodev.status &= ~(1 << 1);
    xTaskNotifyIndexed(audio_play_task_handler, 0, (uint32_t) audiodev.curindex[audiodev.dir_num] + 1,
                       eSetValueWithOverwrite);//发送新的音乐索引
    xTaskNotifyIndexed(audio_play_task_handler, 1, (uint32_t) audiodev.dir_num, eSetValueWithOverwrite);//发送新的音乐索引
}

void Audio_Play_Last(void)
{
    xTaskNotifyIndexed(audio_play_task_handler, 2, 0XFF, eSetValueWithOverwrite);//向音乐播放任务发送通知，通知GUI有操作
    audiodev.status &= ~(1 << 1);
    xTaskNotifyIndexed(audio_play_task_handler, 0, (uint32_t) audiodev.curindex[audiodev.dir_num] - 1,
                       eSetValueWithOverwrite);//发送新的音乐索引
    xTaskNotifyIndexed(audio_play_task_handler, 1, (uint32_t) audiodev.dir_num, eSetValueWithOverwrite);//发送新的音乐索引
}

void Audio_Play_Indexed(uint32_t idx)
{
    xTaskNotifyIndexed(audio_play_task_handler, 2, 0XFF, eSetValueWithOverwrite);//向音乐播放任务发送通知，通知GUI有操作
    audiodev.status &= ~(1 << 1);
    xTaskNotifyIndexed(audio_play_task_handler, 0, (uint32_t) idx, eSetValueWithOverwrite);//发送新的音乐索引
    xTaskNotifyIndexed(audio_play_task_handler, 1, (uint32_t) audiodev.dir_num, eSetValueWithOverwrite);//发送新的音乐索引
}

void Audio_Play_Next_Folder(void)
{
    xTaskNotifyIndexed(audio_play_task_handler, 2, 0XFF, eSetValueWithOverwrite);//向音乐播放任务发送通知，通知GUI有操作
    audiodev.status &= ~(1 << 1);
    audiodev.dir_num++;
    if (audiodev.dir_num > MAX_DIR_NUM - 1) audiodev.dir_num = 0;
    xTaskNotifyIndexed(audio_play_task_handler, 0, (uint32_t) audiodev.curindex[audiodev.dir_num],
                       eSetValueWithOverwrite);//发送新的音乐索引
    xTaskNotifyIndexed(audio_play_task_handler, 1, (uint32_t) audiodev.dir_num, eSetValueWithOverwrite);//发送新的音乐索引
}

void Audio_Play_Last_Folder(void)
{
    xTaskNotifyIndexed(audio_play_task_handler, 2, 0XFF, eSetValueWithOverwrite);//向音乐播放任务发送通知，通知GUI有操作
    audiodev.status &= ~(1 << 1);
    audiodev.dir_num--;
    if (audiodev.dir_num < 0) audiodev.dir_num = MAX_DIR_NUM - 1;
    xTaskNotifyIndexed(audio_play_task_handler, 0, (uint32_t) audiodev.curindex[audiodev.dir_num],
                       eSetValueWithOverwrite);//发送新的音乐索引
    xTaskNotifyIndexed(audio_play_task_handler, 1, (uint32_t) audiodev.dir_num, eSetValueWithOverwrite);//发送新的音乐索引
}

void Audio_Change_Folder(uint8_t idx)
{
    if (idx > 5) return;
    xTaskNotifyIndexed(audio_play_task_handler, 2, 0XFF, eSetValueWithOverwrite);//向音乐播放任务发送通知，通知GUI有操作
    audiodev.status &= ~(1 << 1);
    audiodev.dir_num = idx;
    xTaskNotifyIndexed(audio_play_task_handler, 0, (uint32_t) audiodev.curindex[audiodev.dir_num],
                       eSetValueWithOverwrite);//发送新的音乐索引
    xTaskNotifyIndexed(audio_play_task_handler, 1, (uint32_t) audiodev.dir_num, eSetValueWithOverwrite);//发送新的音乐索引
}