#ifndef __FLACPLAY_H__
#define __FLACPLAY_H__

#include <inttypes.h>
#include <string.h>
#include "flacdecoder.h"
#include <sys/cdefs.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <dirent.h>   //定义了用于遍历目录结构的类型和函数
#include <sys/stat.h> //提供了用于获取文件或目录状态信息的接口
#include "sdcard.h"

//flaC 标签 
typedef __packed struct
{
    uint8_t id[3];            //ID,在文件起始位置,必须是flaC 4个字母
} FLAC_Tag;

//metadata 数据块头信息结构体 
typedef __packed struct
{
    uint8_t head;            //metadata block头
    uint8_t size[3];            //metadata block数据长度
} MD_Block_Head;


//FLAC控制结构体
typedef __packed struct
{
    uint32_t totsec;                //整首歌时长,单位:秒
    uint32_t cursec;                //当前播放时长

    uint32_t bitrate;                //比特率
    uint32_t samplerate;                //采样率
    uint16_t outsamples;                //PCM输出数据量大小
    uint16_t bps;                    //位数,比如16bit,24bit,32bit

    uint32_t datastart;                //数据帧开始的位置(在文件里面的偏移)
} __flacctrl;

extern __flacctrl *flacctrl;
extern volatile uint8_t flac_which_buf;

uint8_t Flac_Init(FILE *fx, __flacctrl *fctrl, FLACContext *fc);

void Flac_I2S_DMA_TX_Callback(void *arg);

void Flac_Get_Curtime(FILE *fx, __flacctrl *flacx);

uint8_t Flac_Play_Song(uint8_t *fname, uint8_t isDecodedIntoFile);

#endif




























