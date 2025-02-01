//
// Created by Misaka on 24-10-18.
//

#include <esp_err.h>
#include <string.h>
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include "sdcard.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>   //定义了用于遍历目录结构的类型和函数
#include <sys/stat.h> //提供了用于获取文件或目录状态信息的接口
#include <driver/sdmmc_host.h>

#define TAG     "sdcard"

//文件类型列表
#define FILE_MAX_TYPE_NUM        7    //最多FILE_MAX_TYPE_NUM个大类
#define FILE_MAX_SUBT_NUM        5    //最多FILE_MAX_SUBT_NUM个小类

//文件类型列表
char *const FILE_TYPE_TBL[FILE_MAX_TYPE_NUM][FILE_MAX_SUBT_NUM] =
        {
                {"BIN"},            //BIN文件
                {"LRC"},            //LRC文件
                {"NES"},            //NES文件
                {"TXT", "C",   "H"},    //文本文件
                {"WAV", "MP3", "APE",  "FLAC", "FLA"},//支持的音乐文件
                {"BMP", "JPG", "JPEG", "GIF"},//图片文件
                {"AVI"},            //视频文件
        };

sdmmc_card_t *card_info;

esp_err_t SDCard_Fatfs_Init(void)
{
    esp_err_t ret = ESP_OK;

    //挂载SD卡
    esp_vfs_fat_mount_config_t mount_config = {
            .allocation_unit_size = 16 * 1024,
            .max_files = 10,
            .format_if_mount_failed = false,
            .disk_status_check_enable = false
    };
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing the SDCard...");
    ESP_LOGI(TAG, "Using SDIO peripheral");

    sdmmc_host_t host = {\
    .flags = SDMMC_HOST_FLAG_8BIT | \
             SDMMC_HOST_FLAG_4BIT | \
             SDMMC_HOST_FLAG_1BIT | \
             SDMMC_HOST_FLAG_DDR, \
    .slot = SDMMC_HOST_SLOT_1, \
    .max_freq_khz = SDMMC_FREQ_HIGHSPEED, \
    .io_voltage = 3.3f, \
    .init = &sdmmc_host_init, \
    .set_bus_width = &sdmmc_host_set_bus_width, \
    .get_bus_width = &sdmmc_host_get_slot_width, \
    .set_bus_ddr_mode = &sdmmc_host_set_bus_ddr_mode, \
    .set_card_clk = &sdmmc_host_set_card_clk, \
    .set_cclk_always_on = &sdmmc_host_set_cclk_always_on, \
    .do_transaction = &sdmmc_host_do_transaction, \
    .deinit = &sdmmc_host_deinit, \
    .io_int_enable = sdmmc_host_io_int_enable, \
    .io_int_wait = sdmmc_host_io_int_wait, \
    .command_timeout_ms = 0, \
    .get_real_freq = &sdmmc_host_get_real_freq, \
    .input_delay_phase = SDMMC_DELAY_PHASE_0, \
    .set_input_delay = &sdmmc_host_set_input_delay, \
    .dma_aligned_buffer = NULL, \
    .pwr_ctrl_handle = NULL, \
    .get_dma_info = &sdmmc_host_get_dma_info, \
};
    sdmmc_slot_config_t slot_config = {\
    .cd = SDMMC_SLOT_NO_CD, \
    .wp = SDMMC_SLOT_NO_WP, \
    .width   = 4, \
    .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP, \
};

    ESP_LOGI(TAG, "Mounting filesystem");
//    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card_info);
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card_info);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. ");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card_info);
    return ret;
}

int Dir_List_File(char *dirpath)
{
    DIR *dir = opendir(dirpath);
    struct dirent *dirent;
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Can't open directory %s", dirpath);
        return -1;
    }
    while ((dirent = readdir(dir)) != NULL)
    {
        printf("%s,  TYPE = %d\n", dirent->d_name, dirent->d_type);
    }
    closedir(dir);
    return 0;
}

//将小写字母转为大写字母,如果是数字,则保持不变.
static char char_upper(char c)
{
    if (c < 'A')return c;//数字,保持不变.
    if (c >= 'a')return c - 0x20;//变为大写.
    else return c;//大写,保持不变
}

//报告文件的类型
//fname:文件名
//返回值:0XFF,表示无法识别的文件类型编号.
//		 其他,高四位表示所属大类,低四位表示所属小类.
uint8_t f_typetell(char *fname)
{
    char tbuf[5];
    char *attr = NULL;//后缀名
    uint8_t i = 0, j;
    while (i < 250)
    {
        i++;
        if (*fname == '\0')break;//偏移到了最后了.
        fname++;
    }
    if (i == 250) return 0XFF;//错误的字符串.
    for (i = 0; i < 5; i++)//得到后缀名
    {
        fname--;
        if (*fname == '.')
        {
            fname++;
            attr = fname;
            break;
        }
    }
    strcpy(tbuf, attr);//copy
    for (i = 0; i < 4; i++) tbuf[i] = char_upper(tbuf[i]);//全部变为大写
    for (i = 0; i < FILE_MAX_TYPE_NUM; i++)    //大类对比
    {
        for (j = 0; j < FILE_MAX_SUBT_NUM; j++)//子类对比
        {
            if (FILE_TYPE_TBL[i][j] == NULL)break;//此组已经没有可对比的成员了.
            if (strcmp(FILE_TYPE_TBL[i][j], tbuf) == 0)//找到了
            {
                return (i << 4) | j;
            }
        }
    }
    return 0XFF;//没找到
}

uint64_t Get_File_Size(char *fname)
{
    struct stat fstat;
    stat(fname, &fstat);
    return fstat.st_size;
}

void SDCard_RW_Test(void)
{
    char data[256] = {0};
    char data_r[256] = {0};
    FILE *test_file;

    sprintf(data, "Hello From ESP32\n");

    printf("进行单文件读写测试:\n");
    test_file = fopen("/sdcard/hello.txt", "w+");
    fwrite(data, sizeof(char), strlen(data), test_file);
    fclose(test_file);

    test_file = fopen("/sdcard/hello.txt", "r");
    fread(data_r, sizeof(char), strlen(data), test_file);
    printf("%s", data_r);
    fclose(test_file);

    printf("OK\n");

    printf("进行目录测试:\n");
    Dir_List_File("/sdcard/MUSIC");


}





