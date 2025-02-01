#ifndef __MP3PLAY_H
#define __MP3PLAY_H

#include <sys/cdefs.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <dirent.h>   //定义了用于遍历目录结构的类型和函数
#include <sys/stat.h> //提供了用于获取文件或目录状态信息的接口
#include "sdcard.h"

#define MP3_TITSIZE_MAX        40        //歌曲名字最大长度
#define MP3_ARTSIZE_MAX        40        //歌曲名字最大长度
#define MP3_FILE_BUF_SZ    5*1024    //MP3解码时,文件buf大小

//ID3V1 标签 
typedef __packed struct
{
    uint8_t id[3];            //ID,TAG三个字母
    uint8_t title[30];        //歌曲名字
    uint8_t artist[30];        //艺术家名字
    uint8_t year[4];            //年代
    uint8_t comment[30];        //备注
    uint8_t genre;            //流派
} ID3V1_Tag;

//ID3V2 标签头 
typedef __packed struct
{
    uint8_t id[3];            //ID
    uint8_t mversion;        //主版本号
    uint8_t sversion;        //子版本号
    uint8_t flags;            //标签头标志
    uint8_t size[4];            //标签信息大小(不包含标签头10字节).所以,标签大小=size+10.
} ID3V2_TagHead;

//ID3V2.3 版本帧头
typedef __packed struct
{
    uint8_t id[4];            //帧ID
    uint8_t size[4];            //帧大小
    uint16_t flags;            //帧标志
} ID3V23_FrameHead;

//MP3 Xing帧信息(没有全部列出来,仅列出有用的部分)
typedef __packed struct
{
    uint8_t id[4];            //帧ID,为Xing/Info
    uint8_t flags[4];        //存放标志
    uint8_t frames[4];        //总帧数
    uint8_t fsize[4];        //文件总大小(不包含ID3)
} MP3_FrameXing;

//MP3 VBRI帧信息(没有全部列出来,仅列出有用的部分)
typedef __packed struct
{
    uint8_t id[4];            //帧ID,为Xing/Info
    uint8_t version[2];        //版本号
    uint8_t delay[2];        //延迟
    uint8_t quality[2];        //音频质量,0~100,越大质量越好
    uint8_t fsize[4];        //文件总大小
    uint8_t frames[4];        //文件总帧数
} MP3_FrameVBRI;


//MP3控制结构体
typedef __packed struct
{
    uint8_t title[MP3_TITSIZE_MAX];    //歌曲名字
    uint8_t artist[MP3_ARTSIZE_MAX];    //艺术家名字
    uint32_t totsec;                //整首歌时长,单位:秒
    uint32_t cursec;                //当前播放时长

    uint32_t bitrate;                //比特率
    uint32_t samplerate;                //采样率
    uint16_t outsamples;                //PCM输出数据量大小(以16位为单位),单声道MP3,则等于实际输出*2(方便DAC输出)

    uint32_t datastart;                //数据帧开始的位置(在文件里面的偏移)
} __mp3ctrl;

extern __mp3ctrl *mp3ctrl;
extern volatile uint8_t mp3_transfer_completed;    /* i2s传输完成标志 */
extern volatile uint8_t mp3_which_buf;       /* i2sbufx指示标志 */


void MP3_I2S_DMA_TX_Callback(void *arg);

void MP3_Fill_Buffer(const uint16_t *buf, uint16_t size, uint8_t nch);

uint8_t MP3_Id3v1_Decode(uint8_t *buf, __mp3ctrl *pctrl);

uint8_t MP3_Id3v2_Decode(uint8_t *buf, uint32_t size, __mp3ctrl *pctrl);

uint8_t MP3_Get_Info(uint8_t *pname, __mp3ctrl *pctrl);

uint8_t MP3_Play_Song(uint8_t *fname, uint8_t isDecodeIntoFile);

void MP3_Get_Current_Time(FILE *fx, __mp3ctrl *mp3x);

uint32_t MP3_File_Seek(uint32_t pos);

#endif
#ifndef AUDIO_MIN
#define AUDIO_MIN(x, y)    ((x)<(y)? (x):(y))
#endif
