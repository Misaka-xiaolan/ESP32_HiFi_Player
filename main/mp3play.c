#define OPEN_MP3

#ifdef OPEN_MP3

#include <string.h>
#include "mp3play.h"
#include "audioplay.h"
#include "mp3dec.h"
#include "freertos_task.h"
#include "audio_resample.h"
#include "bluetooth.h"
#include "cs4398.h"

#include <sys/types.h>
#include <unistd.h>

uint16_t mp3_count;
__mp3ctrl *mp3ctrl;
volatile uint8_t mp3_which_buf = 0;       /* i2sbufx指示标志 */

/*
 * MP3有线播放时的DMA中断回调函数
 */
void IRAM_ATTR MP3_I2S_DMA_TX_Callback(void *arg)
{
    uint8_t *next_buf;
    i2s_hal_clear_intr_status(&hi2s, (1UL << 11)); //清空中断标志位
    i2s_hal_clear_intr_status(&hi2s, I2S_LL_TX_EVENT_MASK); //清空中断标志位
    if (DMA_list[0].buffer_ptr == (uint32_t) audiodev.i2sbuf1) //判断当前传输完成的缓冲区是i2sbuf1还是i2sbuf2
    {
        if ((audiodev.status & 0X01) == 0) //暂停了,填充0
        {
            for (uint16_t i = 0; i < 2304 * 2; i++) audiodev.i2sbuf1[i] = 0;
        }
        mp3_which_buf = 0; //下一次该填充i2sbuf1了
        next_buf = audiodev.i2sbuf2; //下一次该发送i2sbuf2了
    }
    else
    {
        if ((audiodev.status & 0X01) == 0) //暂停了,填充0
        {
            for (uint16_t i = 0; i < 2304 * 2; i++) audiodev.i2sbuf2[i] = 0;
        }
        mp3_which_buf = 1; //下一次该填充i2sbuf2了
        next_buf = audiodev.i2sbuf1; //下一次该发送i2sbuf1了
    }
    xSemaphoreGiveFromISR(semphore_handle, NULL); //开始下一次解码
    I2S_DMA_Buffer_Reload(next_buf, 2304 * 2); //重装载DMA链表
    I2S_DMA_Transmit_Start(); //开始下一次DMA传输
}


uint32_t MP3_File_Seek(uint32_t pos)
{
    if (pos > audiodev.filesize)
    {
        pos = audiodev.filesize;
    }
    fseek(audiodev.file, (long) pos, SEEK_CUR);
    return ftell(audiodev.file);
}

//填充PCM数据到蓝牙或有线
//buf:PCM数据首地址
//size:pcm数据量(16位为单位)
//nch:声道数(1,单声道,2立体声)
void MP3_Fill_Buffer(const uint16_t *buf, uint16_t size, uint8_t nch)
{

    uint32_t i;
    uint16_t *p;
    if (bluetooth_play) //蓝牙传输数据处理部分
    {
        long input_frames = size / sizeof(int16_t); // 双声道帧数
        float src_ratio = (float) 44100 / (float) mp3ctrl->samplerate; //设置比率
        long max_output_frames = input_frames * ((double) 44100 / mp3ctrl->samplerate) + 1; //根据比率计算最大的帧数
        if (mp3ctrl->samplerate == 44100)
        {
            max_output_frames--;
        }
        audiodev.bt_outputframes = max_output_frames; //实际输出帧数
        int16_t *output_buffer;
        int16_t *resample_temp_output;

        if (!audiodev.btbuf1 || !audiodev.btbuf2) //分配蓝牙数据处理缓冲区
        {
            audiodev.btbuf1 = (int16_t *) calloc(max_output_frames * 2 * sizeof(int16_t), 1); // 双声道，乘以2
            audiodev.btbuf2 = (int16_t *) calloc(max_output_frames * 2 * sizeof(int16_t), 1); // 双声道，乘以2
        }
        else
        {
            audiodev.btbuf1 = (int16_t *) realloc(audiodev.btbuf1, max_output_frames * 2 * sizeof(int16_t)); // 双声道，乘以2
            audiodev.btbuf2 = (int16_t *) realloc(audiodev.btbuf2, max_output_frames * 2 * sizeof(int16_t)); // 双声道，乘以2
        }
        resample_temp_output = (int16_t *) calloc(max_output_frames * 2 * sizeof(int16_t), 1);
        if (!audiodev.btbuf1 || !audiodev.btbuf2 || !resample_temp_output)
        {
            printf("蓝牙音频缓冲区分配失败\n");
            audiodev.btbuf1 = (int16_t *) audiodev.i2sbuf1;
            audiodev.btbuf2 = (int16_t *) audiodev.i2sbuf2;
            return;
        }
        if (mp3ctrl->samplerate != 44100) //当需要重采样时
        {
            PCM_Chunk_Resample((const int16_t *) buf, input_frames,
                               resample_temp_output, &audiodev.bt_outputframes, src_ratio); //重采样
//            printf("Processed %ld input -> %ld output\n", input_frames, audiodev.bt_outputframes);
        }
        audiodev.file_type = FILE_MP3;
        xSemaphoreTake(semphore_handle, portMAX_DELAY);
        if (!mp3_which_buf)
        {
            output_buffer = audiodev.btbuf1;  //mp3_which_buf = 0
        }
        else
        {
            output_buffer = audiodev.btbuf2;  //mp3_which_buf = 1
        }
        if (mp3ctrl->samplerate != 44100) //当需要重采样时
        {
            for (i = 0; i < audiodev.bt_outputframes * 2; i++)
            {
                output_buffer[i] = resample_temp_output[i];
            }
        }
        else
        {
            for (i = 0; i < audiodev.bt_outputframes * 2; i++) //直接将源数据填充至蓝牙数据缓冲区
            {
                output_buffer[i] = buf[i] / 1;
            }
        }
        free(resample_temp_output);
        return;
    }
    //有线播放数据处理部分
    audiodev.file_type = FILE_MP3;
    xSemaphoreTake(semphore_handle, portMAX_DELAY);
    if (!mp3_which_buf)
    {
        p = (uint16_t *) audiodev.i2sbuf1;

    }
    else
    {
        p = (uint16_t *) audiodev.i2sbuf2;
        if ((audiodev.status & 0X01) == 0) //暂停了,填充0
        {
            for (i = 0; i < 2304 * 2; i++)audiodev.i2sbuf2[i] = 0;
            if (bluetooth_play)
            {
                for (i = 0; i < 2304; i++) audiodev.btbuf2[i] = 0;
            }
            return;
        }
    }
    if (nch == 2)
    {
        for (i = 0; i < size; i++)
        {
            p[i] = buf[i] / 1;
        }
    }
    else    //单声道
    {
        for (i = 0; i < size; i++)
        {
            p[2 * i] = buf[i];
            p[2 * i + 1] = buf[i];
        }
    }

}

//解析ID3V1 
//buf:输入数据缓存区(大小固定是128字节)
//pctrl:MP3控制器
//返回值:0,获取正常
//    其他,获取失败
uint8_t MP3_Id3v1_Decode(uint8_t *buf, __mp3ctrl *pctrl)
{
    ID3V1_Tag *id3v1tag;
    id3v1tag = (ID3V1_Tag *) buf;
    if (strncmp("TAG", (char *) id3v1tag->id, 3) == 0)//是MP3 ID3V1 TAG
    {
        if (id3v1tag->title[0])strncpy((char *) pctrl->title, (char *) id3v1tag->title, 30);
        if (id3v1tag->artist[0])strncpy((char *) pctrl->artist, (char *) id3v1tag->artist, 30);
    }
    else return 1;
    return 0;
}

//解析ID3V2 
//buf:输入数据缓存区
//size:数据大小
//pctrl:MP3控制器
//返回值:0,获取正常
//    其他,获取失败
uint8_t MP3_Id3v2_Decode(uint8_t *buf, uint32_t size, __mp3ctrl *pctrl)
{
    ID3V2_TagHead *taghead;
    ID3V23_FrameHead *framehead;
    uint32_t t;
    uint32_t tagsize;    //tag大小
    uint32_t frame_size;    //帧大小
    taghead = (ID3V2_TagHead *) buf;
    if (strncmp("ID3", (const char *) taghead->id, 3) == 0)//存在ID3?
    {
        tagsize = ((uint32_t) taghead->size[0] << 21) | ((uint32_t) taghead->size[1] << 14) |
                  ((uint16_t) taghead->size[2] << 7) | taghead->size[3];//得到tag 大小
        pctrl->datastart = tagsize;        //得到mp3数据开始的偏移量
        if (tagsize > size)tagsize = size;    //tagsize大于输入bufsize的时候,只处理输入size大小的数据
        if (taghead->mversion < 3)
        {
            printf("not supported mversion!\r\n");
            return 1;
        }
        t = 10;
        while (t < tagsize)
        {
            framehead = (ID3V23_FrameHead *) (buf + t);
            frame_size = ((uint32_t) framehead->size[0] << 24) | ((uint32_t) framehead->size[1] << 16) |
                         ((uint32_t) framehead->size[2] << 8) | framehead->size[3];//得到帧大小
            if (strncmp("TT2", (char *) framehead->id, 3) == 0 ||
                strncmp("TIT2", (char *) framehead->id, 4) == 0) //找到歌曲标题帧,不支持unicode格式!!
            {
                strncpy((char *) pctrl->title, (char *) (buf + t + sizeof(ID3V23_FrameHead) + 1),
                        AUDIO_MIN(frame_size - 1, MP3_TITSIZE_MAX - 1));
            }
            if (strncmp("TP1", (char *) framehead->id, 3) == 0 ||
                strncmp("TPE1", (char *) framehead->id, 4) == 0)//找到歌曲艺术家帧
            {
                strncpy((char *) pctrl->artist, (char *) (buf + t + sizeof(ID3V23_FrameHead) + 1),
                        AUDIO_MIN(frame_size - 1, MP3_ARTSIZE_MAX - 1));
            }
            t += frame_size + sizeof(ID3V23_FrameHead);
        }
    }
    else
    {
        printf("不存在ID3,mp3数据是从0开始\n");
        pctrl->datastart = 0;//不存在ID3,mp3数据是从0开始
    }
    return 0;
}

uint16_t countt = 0;

uint8_t MP3_Play_Song(uint8_t *fname, uint8_t isDecodeIntoFile)
{
    HMP3Decoder mp3decoder;
    MP3FrameInfo mp3frameinfo;
    uint8_t res = 0;
    uint8_t *buffer;        //输入buffer
    uint8_t *readptr;    //MP3解码读指针
    FILE **pcmfile;
    int offset = 0;    //偏移量
    int outofdata = 0;//超出数据范围
    int bytesleft = 0;//buffer还剩余的有效数据
    uint32_t br = 0;
    int err = 0;
    if (bluetooth_play)
    {
        while (Get_s_media_state() != APP_AV_MEDIA_STATE_STARTED)
        {
            vTaskDelay(100);
        }
    }
    if (isDecodeIntoFile == 1)
    {
        pcmfile = malloc(sizeof(FILE));
    }
    mp3ctrl = malloc(sizeof(__mp3ctrl));
    buffer = malloc(MP3_FILE_BUF_SZ); //申请解码buf大小
    audiodev.i2sbuf1 = heap_caps_malloc(2304 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    audiodev.i2sbuf2 = heap_caps_malloc(2304 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    audiodev.tbuf = malloc(2304 * 2);
    audiodev.file_seek = MP3_File_Seek;
    if (!mp3ctrl || !buffer || !audiodev.i2sbuf1 || !audiodev.i2sbuf2 || !audiodev.tbuf)//内存申请失败
    {
        free(mp3ctrl);
        free(buffer);
        free(audiodev.i2sbuf1);
        free(audiodev.i2sbuf2);
        free(audiodev.tbuf);
        printf("mp3播放时,申请内存错误\n");
        return AP_ERR;    //错误
    }
    memset(audiodev.i2sbuf1, 0, 2304 * 2);    //数据清零
    memset(audiodev.i2sbuf2, 0, 2304 * 2);    //数据清零
    memset(mp3ctrl, 0, sizeof(__mp3ctrl));//数据清零
    audiodev.filesize = Get_File_Size((char *) fname);
    res = MP3_Get_Info(fname, mp3ctrl);
    if (res == 0)
    {
        if ((bluetooth_play) == 0)
        {
            CS4398_Power_Set(0, 0); //根据数据手册的配置，MCLKDIV2设置为0
            CS4398_Cross_Set(3, 1, 0); //慢速滤波（内部低通滤波）
            CS4398_I2S_Cfg(1, 0, 0); //飞利浦标准，最高24位
            I2S_DMA_Init(MP3_I2S_DMA_TX_Callback); //初始化DMA和回调
            I2S_Set_Samplerate_and_Bitdepth(mp3ctrl->samplerate, 16); //设置位深度和采样率
            I2S_DMA_Buffer_Reload(audiodev.i2sbuf1, 2304 * 2); //预填充DMA链表
        }
        if (res) //不支持的采样率
        {
            fclose(audiodev.file);
            free(mp3ctrl);
            free(buffer);
            free(audiodev.i2sbuf1);
            free(audiodev.i2sbuf2);
            free(audiodev.tbuf);
            printf("mp3播放时,不支持的采样率\n");
            return AP_ERR;
        }
        mp3decoder = MP3InitDecoder();                    //MP3解码申请内存
        audiodev.file = fopen((char *) fname, "r");
        if (audiodev.file == NULL)
        {
            res = 1;
        }
    }
    else
    {
        printf("mp3播放时,获取歌曲信息失败\n");
        return AP_ERR;
    }

    printf("MP3 文件信息: \n");
    printf(" 歌曲名称: %s \n", mp3ctrl->title);
    printf(" 艺术家名称: %s \n", mp3ctrl->artist);
    printf(" 总时长: %lu s \n", mp3ctrl->totsec);
    printf(" 比特率: %lu kbps \n", mp3ctrl->bitrate / 1000);
    printf(" 采样率: %lu \n", mp3ctrl->samplerate);

    if (isDecodeIntoFile == 1)
    {
        *pcmfile = fopen("/sdcard/mp3decoded.pcm", "wb+");
        if (*pcmfile == NULL)
        {
            res = 1;
        }
    }
    if (res == 0 && mp3decoder != 0)//打开文件成功
    {
        if ((bluetooth_play) == 0) I2S_DMA_Transmit_Start(); //开启一次DMA传输
        fseek(audiodev.file, (long) mp3ctrl->datastart, SEEK_CUR);
        while (res == 0)
        {
            readptr = buffer;    //MP3读指针指向buffer
            //偏移量为0
            outofdata = 0;    //数据正常
            bytesleft = 0;
            br = fread(buffer, 1, MP3_FILE_BUF_SZ, audiodev.file);
            if (ferror(audiodev.file) != 0)
            {
                res = 1;
            }
            if (res)//读数据出错了
            {
                res = AP_ERR;
                printf("mp3解码时,读取mp3文件失败\n");
                break;
            }
            if (br == 0)        //读数为0,说明解码完成了.
            {
                res = 2;    //播放完成
                printf("mp3播放完成\n");
                break;
            }
            bytesleft += (int) br;    //buffer里面有多少有效MP3数据?
            while (!outofdata)//没有出现数据异常(即可否找到帧同步字符)
            {
                offset = MP3FindSyncWord(readptr, bytesleft);//在readptr位置,开始查找同步字符
                if (offset < 0)    //没有找到同步字符,跳出帧解码循环
                {
                    printf("mp3解码时,没有找到帧同步字\n");
                    outofdata = 1;//没找到帧同步字符
                }
                else    //找到同步字符了
                {
                    readptr += offset;        //MP3读指针偏移到同步字符处.
                    bytesleft -= offset;        //buffer里面的有效数据个数,必须减去偏移量
                    err = MP3Decode(mp3decoder, &readptr, &bytesleft, (short *) audiodev.tbuf, 0);//解码一帧MP3数据
                    if (err != 0)//解码错误，不能直接自动切换下一首，因为有可能是快进退
                    {
                        break;
                    }
                    else
                    {
                        MP3GetLastFrameInfo(mp3decoder, &mp3frameinfo);    //得到刚刚解码的MP3帧信息
                        if (mp3ctrl->bitrate != mp3frameinfo.bitrate)        //更新码率
                        {
                            mp3ctrl->bitrate = mp3frameinfo.bitrate;
                        }
                        if (isDecodeIntoFile == 1)
                        {
                            fwrite(audiodev.tbuf, 1, mp3frameinfo.outputSamps * 2, *pcmfile);
                            countt++;
                        }
                        else
                            MP3_Fill_Buffer((uint16_t *) audiodev.tbuf, mp3frameinfo.outputSamps,
                                            mp3frameinfo.nChans);//填充pcm数据
                    }
                    if (bytesleft < MAINBUF_SIZE * 2)//当数组内容小于2倍MAINBUF_SIZE的时候,必须补充新的数据进来.
                    {
                        memmove(buffer, readptr, bytesleft);//移动readptr所指向的数据到buffer里面,数据量大小为:bytesleft
                        br = fread(buffer + bytesleft, 1, MP3_FILE_BUF_SZ - bytesleft, audiodev.file);
                        if (br < MP3_FILE_BUF_SZ - bytesleft)
                        {
                            memset(buffer + bytesleft + br, 0, MP3_FILE_BUF_SZ - bytesleft - br);
                        }
                        bytesleft = MP3_FILE_BUF_SZ;
                        readptr = buffer;
                    }
                    while (audiodev.status & (1 << 1))//正常播放中
                    {
                        MP3_Get_Current_Time(audiodev.file, mp3ctrl);
                        audiodev.totsec = mp3ctrl->totsec;    //参数传递
                        audiodev.cursec = mp3ctrl->cursec;
                        audiodev.bitrate = mp3ctrl->bitrate;
                        audiodev.samplerate = mp3ctrl->samplerate;
                        audiodev.bps = 16;//MP3仅支持16位
                        if (audiodev.status & 0X01)break;//没有按下暂停
                        else
                        {
                            if (bluetooth_play == 1)
                            {
                                memset(audiodev.btbuf1, 0, audiodev.bt_outputframes * 2 * sizeof(int16_t));
                                memset(audiodev.btbuf2, 0, audiodev.bt_outputframes * 2 * sizeof(int16_t));
                            }
                            vTaskDelay(20);
                        }
                    }
                    if ((audiodev.status & (1 << 1)) == 0)//请求结束播放/播放完成
                    {
                        res = AP_NEXT;//跳出上上级循环
                        outofdata = 1;//跳出上一级循环
                        break;
                    }
                }
                mp3_count++;
//                printf("mp3解码进度: %d\n当前时间: %ld\n总时间: %ld\n\n", mp3_count, audiodev.cursec, audiodev.totsec);
            }
        }
        if ((bluetooth_play) == 0)
        {
            CS4398_Power_Set(1, 0);
            I2S_DMA_Transmit_Stop(); //DMA停止发送
        }
    }
    else
    {
        res = AP_ERR;//错误
        printf("mp3播放时,打开mp3文件失败\n");
    }
    audiodev.file_type = FILE_NONE;
    mp3_count = 0;
    fclose(audiodev.file);
    if (isDecodeIntoFile == 1) fclose(*pcmfile);
    MP3FreeDecoder(mp3decoder);        //释放内存
    free(mp3ctrl);
    free(buffer);
    free(audiodev.i2sbuf1);
    free(audiodev.i2sbuf2);
    free(audiodev.btbuf1);
    free(audiodev.btbuf2);
    audiodev.btbuf1 = NULL;
    audiodev.btbuf2 = NULL;
    free(audiodev.tbuf);
    audiodev.tbuf = NULL;
    audiodev.i2sbuf1 = NULL;
    audiodev.i2sbuf2 = NULL;
    free(audiodev.bt_tempbuf);
    free(audiodev.bt_sendbuf);
    audiodev.bt_tempbuf = NULL;
    audiodev.bt_sendbuf = NULL;
    if (isDecodeIntoFile == 1) free(pcmfile);
    return res;
}


uint8_t MP3_Get_Info(uint8_t *pname, __mp3ctrl *pctrl)
{
    HMP3Decoder decoder;
    MP3FrameInfo frame_info;
    MP3_FrameXing *fxing;
    MP3_FrameVBRI *fvbri;
    FILE *fmp3 = NULL;
    uint8_t *buf = NULL;
    uint32_t br;
    uint8_t res = 0;
    int offset = 0;
    uint32_t p;
    short samples_per_frame;    //一帧的采样个数
    uint32_t totframes;                //总帧数
    buf = malloc(5 * 1024);        //申请5K内存
    if (buf)//内存申请成功
    {
        fmp3 = fopen((char *) pname, "r");
        br = fread(buf, 1, 5 * 1024, fmp3);
        if (ferror(fmp3) != 0)
        {
            res = 1;
        }
        else
        {
            res = 0;
        }
        if (res == 0)//读取文件成功,开始解析ID3V2/ID3V1以及获取MP3信息
        {
            MP3_Id3v2_Decode(buf, br, pctrl);    //解析ID3V2数据
            fseek(fmp3, -128, SEEK_END); //偏移到倒数128的位置
            br = fread(buf, 1, 128, fmp3); //读取128字节
            MP3_Id3v1_Decode(buf, pctrl);    //解析ID3V1数据
            decoder = MP3InitDecoder();        //MP3解码申请内存
            fseek(fmp3, pctrl->datastart, SEEK_SET); //偏移到数据开始的地方
            br = fread(buf, 1, 5 * 1024, fmp3); //读取5K字节mp3数据
            offset = MP3FindSyncWord(buf, br);    //查找帧同步信息
            if (offset >= 0 && MP3GetNextFrameInfo(decoder, &frame_info, &buf[offset]) == 0)//找到帧同步信息了,且下一阵信息获取正常
            {
                p = offset + 4 + 32;
                fvbri = (MP3_FrameVBRI *) (buf + p);
                if (strncmp("VBRI", (char *) fvbri->id, 4) == 0)//存在VBRI帧(VBR格式)
                {
                    if (frame_info.version == MPEG1)samples_per_frame = 1152;//MPEG1,layer3每帧采样数等于1152
                    else samples_per_frame = 576;//MPEG2/MPEG2.5,layer3每帧采样数等于576
                    totframes = ((uint32_t) fvbri->frames[0] << 24) | ((uint32_t) fvbri->frames[1] << 16) |
                                ((uint16_t) fvbri->frames[2] << 8) | fvbri->frames[3];//得到总帧数
                    pctrl->totsec = totframes * samples_per_frame / frame_info.samprate;//得到文件总长度
                }
                else    //不是VBRI帧,尝试是不是Xing帧(VBR格式)
                {
                    if (frame_info.version == MPEG1)    //MPEG1
                    {
                        p = frame_info.nChans == 2 ? 32 : 17;
                        samples_per_frame = 1152;    //MPEG1,layer3每帧采样数等于1152
                    }
                    else
                    {
                        p = frame_info.nChans == 2 ? 17 : 9;
                        samples_per_frame = 576;        //MPEG2/MPEG2.5,layer3每帧采样数等于576
                    }
                    p += offset + 4;
                    fxing = (MP3_FrameXing *) (buf + p);
                    if (strncmp("Xing", (char *) fxing->id, 4) == 0 ||
                        strncmp("Info", (char *) fxing->id, 4) == 0)//是Xng帧
                    {
                        if (fxing->flags[3] & 0X01)//存在总frame字段
                        {
                            totframes = ((uint32_t) fxing->frames[0] << 24) | ((uint32_t) fxing->frames[1] << 16) |
                                        ((uint16_t) fxing->frames[2] << 8) | fxing->frames[3];//得到总帧数
                            pctrl->totsec = totframes * samples_per_frame / frame_info.samprate;//得到文件总长度
                        }
                        else    //不存在总frames字段
                        {
                            pctrl->totsec = audiodev.filesize / (frame_info.bitrate / 8);
                        }
                    }
                    else        //CBR格式,直接计算总播放时间
                    {
                        pctrl->totsec = audiodev.filesize / (frame_info.bitrate / 8);
                    }
                }
                pctrl->bitrate = frame_info.bitrate;            //得到当前帧的码率
                mp3ctrl->samplerate = frame_info.samprate;    //得到采样率.
                if (frame_info.nChans == 2)mp3ctrl->outsamples = frame_info.outputSamps; //输出PCM数据量大小
                else mp3ctrl->outsamples = frame_info.outputSamps * 2; //输出PCM数据量大小,对于单声道MP3,直接*2,补齐为双声道输出
            }
            else
            {
                printf("mp3读取信息时,未找到同步帧\n");
                res = 0XFE;//未找到同步帧
            }
            MP3FreeDecoder(decoder);//释放内存
        }
        fclose(fmp3);
    }
    else
    {
        printf("mp3读取信息时,打开文件失败\n");
        res = 0XFF;
        return res;
    }
    free(buf);
    return res;
}

void MP3_Get_Current_Time(FILE *fx, __mp3ctrl *mp3x)
{
    uint32_t fpos = 0;
    if (ftell(fx) > mp3x->datastart) fpos = ftell(fx) - mp3x->datastart;    //得到当前文件播放到的地方
    mp3x->cursec = fpos * mp3x->totsec / (audiodev.filesize - mp3x->datastart);    //当前播放到第多少秒了?
}

#endif
