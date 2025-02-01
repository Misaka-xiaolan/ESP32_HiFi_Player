//
// Created by Misaka on 24-10-18.
//

#ifndef ST7789_AUDIOPLAY_H
#define ST7789_AUDIOPLAY_H

#include <sys/cdefs.h>
#include "nvs_flash.h"
#include "stdio.h"

#define MAX_DIR_NUM     6

//音乐播放操作结果定义
typedef enum
{
    AP_OK = 0X00,                //正常播放完成
    AP_NEXT,                //播放下一曲
    AP_PREV,                //播放上一曲
    AP_ERR = 0X80,        //播放有错误(没定义错误代码,仅仅表示出错)
    AP_RS = 0X10,     //不支持的错误码率
    AP_DEC_ERR = 0X20,     //解码错误
} APRESULT;

typedef enum
{
    FILE_NONE = 0,
    FILE_MP3,
    FILE_FLAC_16Bit,
    FILE_FLAC_24Bit,
    FILE_WAV_16Bit,
    FILE_WAV_24Bit,
} FILETYPE;

typedef struct
{
    //2个I2S解码的BUF
    uint8_t *i2sbuf1;
    uint8_t *i2sbuf2;
    int16_t *btbuf1;
    int16_t *btbuf2;
    int16_t *bt_sendbuf;
    int16_t *bt_tempbuf;
    uint8_t *tbuf;            //零时数组
    long bt_outputframes;
    FILE *file;            //音频文件指针
    uint32_t (*file_seek)(uint32_t);//文件快进快退函数

    uint8_t status;
    //bit0:0,暂停播放;1,继续播放
    //bit1:0,结束播放;1,开启播放
    //bit2:0退出了音乐播放应用，1：还在音乐应用
    //bit3:0,蓝牙输出关闭;1,蓝牙输出打开
    //bit4:0,无音乐播放;1,音乐播放中 (对外标记)
    //bit5:0,无动作;1,执行了一次切歌操作(对外标记)
    //bit6:0,无动作;1,请求终止播放(但是不删除音频播放任务),处理完成后,播放任务自动清零该位
    //bit7:0,音频播放任务已删除/请求删除;1,音频播放任务正在运行(允许继续执行)
    uint8_t file_type;
    //0:没有文件在播放
    //1:mp3
    //2:flac
    //3:wav
    uint8_t mode;            //播放模式
    //0,全部循环;1,单曲循环;2,随机播放;
    int32_t vol;
    uint8_t *path;            //当前文件夹路径
    uint8_t *name;            //当前播放的MP3歌曲名字
    uint16_t namelen;        //name所占的点数
    uint64_t filesize;      //文件大小
    uint16_t curnamepos;        //当前的偏移

    uint32_t totsec;        //整首歌时长,单位:秒
    uint32_t cursec;        //当前播放时长
    uint32_t bitrate;        //比特率(位速)
    uint32_t samplerate;        //采样率
    uint16_t bps;            //位数,比如16bit,24bit,32bit

    int32_t dir_num;          //当前播放的目录号 0~5
    int32_t curindex[MAX_DIR_NUM];        //当前播放的音频文件索引（六个目录）
    uint16_t mfilenum[MAX_DIR_NUM];        //音乐文件数目（六个目录）
    uint32_t *mfindextbl[MAX_DIR_NUM];    //音频文件索引表（六个目录）

} __audiodev;

extern volatile __audiodev audiodev;
extern volatile uint8_t bluetooth_play;
extern nvs_handle_t audioplayer_storage;

extern uint16_t Audio_Get_Total_Num(char *path);

extern uint8_t *gui_path_name(uint8_t *pname, uint8_t *path, uint8_t *name);

//extern uint8_t Audio_Play_Task_Create(void);
extern uint8_t Audio_Play(void);

extern void Audio_Play_Next(void);

extern void Audio_Play_Last(void);

extern void Audio_Play_Indexed(uint32_t idx);

extern void Audio_Play_Task_Delete(void);

extern void Audio_Play_Next_Folder(void);

extern void Audio_Play_Last_Folder(void);

extern void Audio_Change_Folder(uint8_t idx);

#endif //ST7789_AUDIOPLAY_H
