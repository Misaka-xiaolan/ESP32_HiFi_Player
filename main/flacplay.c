#include "flacplay.h"
#include "audioplay.h"
#include "audio_resample.h"
#include "freertos_task.h"
#include "bluetooth.h"
#include "cs4398.h"
#include <sys/types.h>
#include <unistd.h>

//本程序移植自RockBox的flac解码库

__flacctrl *flacctrl;    //flac解码控制结构体
uint16_t flaccount = 0;

volatile uint8_t flac_which_buf = 0;        //i2sbufx指示标志

//分析FLAC文件
//fx:flac文件指针
//fc:flac解码容器
//返回值:0,分析成功
//    其他,错误代码
uint8_t Flac_Init(FILE *fx, __flacctrl *fctrl, FLACContext *fc)
{
    FLAC_Tag *flactag;
    MD_Block_Head *flacblkh;
    uint8_t *buf;
    uint8_t endofmetadata = 0;            //最后一个metadata标记
    int blocklength;
    uint32_t br;

    buf = malloc(512);    //申请512字节内存
    if (!buf)return 1;            //内存申请失败
    fseek(fx, 0, SEEK_SET);                //偏移到文件头
    br = fread(buf, 1, 4, fx);        //读取4字节
    flactag = (FLAC_Tag *) buf;        //强制转换为flac tag标签
    if (strncmp("fLaC", (char *) flactag->id, 4) != 0)
    {
        free(buf);        //释放内存
        printf("FLAC分析时: 非flac文件\n");
        return 2;                //非flac文件
    }
    while (!endofmetadata)
    {
        br = fread(buf, 1, 4, fx);
        if (br < 4)
        {
            break;
        }
        flacblkh = (MD_Block_Head *) buf;
        endofmetadata = flacblkh->head & 0X80;    //判断是不是最后一个block?
        blocklength = (int) ((uint32_t) flacblkh->size[0] << 16) | ((uint16_t) flacblkh->size[1] << 8) |
                      (flacblkh->size[2]);//得到块大小
        if ((flacblkh->head & 0x7f) == 0)        //head最低7位为0,则表示是STREAMINFO块
        {
            br = fread(buf, 1, blocklength, fx);
            if (ferror(fx) != 0)
            {
                break;
            }
            fc->min_blocksize = ((uint16_t) buf[0] << 8) | buf[1];                    //最小块大小
            fc->max_blocksize = ((uint16_t) buf[2] << 8) | buf[3];                    //最大块大小
            fc->min_framesize = (int) ((uint32_t) buf[4] << 16) | ((uint16_t) buf[5] << 8) | buf[6];//最小帧大小
            fc->max_framesize = (int) ((uint32_t) buf[7] << 16) | ((uint16_t) buf[8] << 8) | buf[9];//最大帧大小
            fc->samplerate =
                    (int) ((uint32_t) buf[10] << 12) | ((uint16_t) buf[11] << 4) | ((buf[12] & 0xf0) >> 4);//采样率
            fc->channels = ((buf[12] & 0x0e) >> 1) + 1;                            //音频通道数
            fc->bps = ((((uint16_t) buf[12] & 0x01) << 4) | ((buf[13] & 0xf0) >> 4)) + 1;    //采样位数16?24?32?
            fc->totalsamples = ((uint32_t) buf[14] << 24) | ((uint32_t) buf[15] << 16) | ((uint16_t) buf[16] << 8) |
                               buf[17];//一个声道的总采样数
            fctrl->samplerate = fc->samplerate;
            fctrl->totsec = (fc->totalsamples / fc->samplerate);//得到总时间
        }
        else    //忽略其他帧的处理
        {
            if (fseek(fx, blocklength, SEEK_CUR) != 0)
            {
                free(buf);
                printf("FLAC分析时: 文件读取错误\n");
                return 3;
            }
        }
    }
    free(buf);//释放内存.
    if (fctrl->totsec)
    {
        fctrl->outsamples = fc->max_blocksize * 2;//PCM输出数据量(*2,表示2个声道的数据量)
        fctrl->bps = fc->bps;            //采样位数(16/24/32)
        fctrl->datastart = ftell(fx);    //FLAC数据帧开始的地址
        fctrl->bitrate = ((audiodev.filesize - fctrl->datastart) * 8) / fctrl->totsec;//得到FLAC的位速
    }
    else
    {
        printf("FLAC分析时: 总时间为0?有问题的flac文件\n");
        return 4;    //总时间为0?有问题的flac文件
    }
    printf("FLAC比特率: %lubps\n", fctrl->bitrate);
    printf("FLAC采样率: %luHz\n", fctrl->samplerate);
    printf("FLAC采样深度: %dbit\n", fctrl->bps);
    return 0;
}

/*
 * FLAC有线播放时的DMA中断回调函数
 */

void IRAM_ATTR Flac_I2S_DMA_TX_Callback(void *arg)
{
    uint16_t i;
    uint16_t size;
    uint8_t *next_buf;
    i2s_hal_clear_intr_status(&hi2s, (1UL << 11)); //清空中断标志位
    i2s_hal_clear_intr_status(&hi2s, I2S_LL_TX_EVENT_MASK); //清空中断标志位
    if (bluetooth_play)
    {
        return;
    }
    if (DMA_list[0].buffer_ptr == (uint32_t) audiodev.i2sbuf1) //判断当前传输完成的缓冲区是i2sbuf1还是i2sbuf2
    {
        flac_which_buf = 0; //下一次该填充i2sbuf1了
        next_buf = audiodev.i2sbuf2; //下一次该发送i2sbuf2了
        if ((audiodev.status & 0X01) == 0)//暂停了,填充0
        {
            if (flacctrl->bps == 24)size = flacctrl->outsamples * 4;
            else size = flacctrl->outsamples * 2;
            for (i = 0; i < size; i++)audiodev.i2sbuf1[i] = 0;
        }
    }
    else
    {
        flac_which_buf = 1; //下一次该填充i2sbuf2了
        next_buf = audiodev.i2sbuf1; //下一次该发送i2sbuf1了
        if ((audiodev.status & 0X01) == 0)//暂停了,填充0
        {
            if (flacctrl->bps == 24)size = flacctrl->outsamples * 4;
            else size = flacctrl->outsamples * 2;
            for (i = 0; i < size; i++)audiodev.i2sbuf2[i] = 0;
        }
    }
    xSemaphoreGiveFromISR(semphore_handle, NULL); //开始下一次解码
    if (flacctrl->bps == 24) I2S_DMA_Buffer_Reload(next_buf, flacctrl->outsamples * 4); //重装载DMA链表
    else if (flacctrl->bps == 16) I2S_DMA_Buffer_Reload(next_buf, flacctrl->outsamples * 2); //重装载DMA链表
    I2S_DMA_Transmit_Start(); //开始下一次DMA传输
}

void FLAC_Fill_Buffer(uint8_t *buf, uint32_t size, uint16_t bps)
{
    if (bluetooth_play) //蓝牙传输数据处理部分
    {
        long input_frames;
        float src_ratio = (float) 44100 / (float) flacctrl->samplerate; //设置比率
        long max_output_frames;
        int16_t *output_buffer;
        int16_t *resample_temp_output;
        if (bps == 16)
        {
            input_frames = size * 4 / 2 / sizeof(int16_t);
        }
        else if (bps == 24)
        {
            input_frames = size * 4 / 2 / sizeof(int16_t);
            for (int i = 0, j = 0; j < size * 2; j++)
            {
                int32_t sample_24bit;
                int16_t sample_16bit;
                int16_t *buf_16bit = (int16_t *) buf;
                /*原数据: 低 中 高 空 低 中 高 空 低 中 高 空 低 中 高 空 低 中 高 空 低 中 高 空*/
                sample_24bit = (int32_t) (buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16));
                if (sample_24bit & 0x800000)
                {
                    sample_24bit |= 0xFF000000;
                }
                sample_16bit = (int16_t) (sample_24bit >> 8);
                buf_16bit[j] = sample_16bit;
                i += 4;
            }
        }
        else
        {
            printf("位深度异常\n");
            return;
        }
        max_output_frames = input_frames * ((double) 44100 / flacctrl->samplerate) + 1;
        if (flacctrl->samplerate == 44100)
        {
            max_output_frames--;
        }
        audiodev.bt_outputframes = max_output_frames;
        if (!audiodev.btbuf1 || !audiodev.btbuf2) //分配蓝牙数据处理缓冲区
        {
            audiodev.btbuf1 = (int16_t *) calloc(max_output_frames * 2 * sizeof(int16_t), 1); // 双声道，乘以2
            audiodev.btbuf2 = (int16_t *) calloc(max_output_frames * 2 * sizeof(int16_t), 1); // 双声道，乘以2
        }
        resample_temp_output = (int16_t *) calloc(max_output_frames * 2 * sizeof(int16_t), 1); // 双声道，乘以2
        if (!audiodev.btbuf1 || !audiodev.btbuf2 || !resample_temp_output)
        {
            printf("蓝牙音频缓冲区分配失败\n");
            audiodev.btbuf1 = (int16_t *) audiodev.i2sbuf1;
            audiodev.btbuf2 = (int16_t *) audiodev.i2sbuf2;
            return;
        }
        if (flacctrl->samplerate != 44100)
        {
            PCM_Chunk_Resample((int16_t *) buf, input_frames,
                               resample_temp_output, &(audiodev.bt_outputframes), src_ratio);
//        printf("Processed %ld input frames -> %ld output frames\n", input_frames, audiodev.bt_outputframes);
        }
        if (bps == 24)
        {
            audiodev.file_type = FILE_FLAC_24Bit;
        }
        else
        {
            audiodev.file_type = FILE_FLAC_16Bit;
        }

        xSemaphoreTake(semphore_handle, portMAX_DELAY);
        if (!flac_which_buf)
        {
            output_buffer = audiodev.btbuf1;  //flac_which_buf = 0
        }
        else
        {
            output_buffer = audiodev.btbuf2;  //flac_which_buf = 1
        }
        if (flacctrl->samplerate != 44100) //当需要重采样时
        {
            for (int i = 0; i < audiodev.bt_outputframes * 2; i++)
            {
                output_buffer[i] = resample_temp_output[i];
            }
        }
        else
        {
            for (int i = 0; i < audiodev.bt_outputframes * 2; i++)
            {
                output_buffer[i] = ((int16_t *) buf)[i];
            }
        }
        free(resample_temp_output);
        return;
    }
    //有线播放数据处理部分
    if (bps == 24)    //24位的时候,数据需要单独处理下
    {
        audiodev.file_type = FILE_FLAC_24Bit;
        for (uint32_t i = 0; i < size * 8;)
        {
            uint8_t temp = 0;
            /*原数据: 低 中 高 空*/
            /*新数据: 中 低 空 高*/
            temp = buf[i];
            buf[i] = buf[i + 1];
            buf[i + 1] = temp;

            buf[i + 3] = buf[i + 2];
            buf[i + 2] = 0;
            i += 4;
        }
    }
    else
    {
        audiodev.file_type = FILE_FLAC_16Bit;
    }
    xSemaphoreTake(semphore_handle, portMAX_DELAY);
}

//得到当前播放时间
//fx:文件指针
//flacctrl:flac播放控制器
void Flac_Get_Curtime(FILE *fx, __flacctrl *flacctrl)
{
    long long fpos = 0;
    if (ftell(fx) > flacctrl->datastart)fpos = ftell(fx) - flacctrl->datastart;    //得到当前文件播放到的地方
    flacctrl->cursec = fpos * flacctrl->totsec / (audiodev.filesize - flacctrl->datastart);    //当前播放到第多少秒了?
}

//flac文件快进快退函数
//pos:需要定位到的文件位置
//返回值:当前文件位置(即定位后的结果)
uint32_t flac_file_seek(uint32_t pos)
{
    if (pos > audiodev.filesize)
    {
        pos = audiodev.filesize;
    }
    fseek(audiodev.file, (long) pos, SEEK_SET);
    return ftell(audiodev.file);
}

//播放一曲FLAC音乐
//fname:FLAC文件路径.
//返回值:0,正常播放完成
//[b7]:0,正常状态;1,错误状态
//[b6:0]:b7=0时,表示操作码 
//       b7=1时,表示有错误(这里不判定具体错误,0X80~0XFF,都算是错误)
extern int vol;
uint8_t Flac_Play_Song(uint8_t *fname, uint8_t isDecodedIntoFile)
{
    FLACContext *fc = 0;
    int bytesleft;
    int consumed;
    uint8_t res = 0;
    uint8_t res1 = 0;
    uint32_t i = 0;
    uint32_t br = 0;
    uint8_t *buffer = 0;
    uint8_t *decbuf0 = 0;
    uint8_t *decbuf1 = 0;
    uint8_t *p8 = 0;
    uint32_t flac_fptr = 0;
    if (bluetooth_play)
    {
        while (Get_s_media_state() != APP_AV_MEDIA_STATE_STARTED) //等待蓝牙设备准备好
        {
            vTaskDelay(100);
        }
    }
    fc = malloc(sizeof(FLACContext));
    flacctrl = malloc(sizeof(__flacctrl));
    audiodev.file_seek = flac_file_seek;
    if (!fc || !flacctrl)
    {
        printf("FLAC解码时: 内存申请错误\n");
        res = 1;//内存申请错误
    }
    else
    {
        memset(fc, 0, sizeof(FLACContext));//fc所有内容清零
        audiodev.file = fopen((char *) fname, "rb");
        if (audiodev.file == NULL)
        {
            res = 1;
        }
        audiodev.filesize = Get_File_Size((char *) fname);
        if (res == 0)
        {
            res = Flac_Init(audiodev.file, flacctrl, fc);    //flac解码初始化
            printf("max_blocksize: %d\n", fc->max_blocksize);
            if (bluetooth_play)
            {
                if (fc->samplerate > 48000 || fc->bps == 24)
                {
                    printf("蓝牙播放模式下不支持48000Hz以上采样率\n");
                    res++;
                }
            }
            if (fc->min_blocksize == fc->max_blocksize && fc->max_blocksize != 0)//必须min_blocksize等于max_blocksize
            {
                if (fc->bps == 24)    //24位音频数据
                {
                    audiodev.i2sbuf1 = heap_caps_malloc(fc->max_blocksize * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
                    audiodev.i2sbuf2 = heap_caps_malloc(fc->max_blocksize * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
                }
                else//16位音频数据
                {
                    audiodev.i2sbuf1 = heap_caps_malloc(fc->max_blocksize * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
                    audiodev.i2sbuf2 = heap_caps_malloc(fc->max_blocksize * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
                }
                buffer = malloc(fc->max_framesize); //申请解码帧缓存
                decbuf0 = malloc(fc->max_blocksize * 4);
                decbuf1 = malloc(fc->max_blocksize * 4);
            }
            else
            {
                printf("FLAC解码时: 不支持的音频格式\n");
                res += 1;//不支持的音频格式
            }
        }
        else
        {
            printf("FLAC解码时: 读取文件错误\n");
        }
    }
    if ((buffer && audiodev.i2sbuf1 && audiodev.i2sbuf2 && decbuf0 && decbuf1) == /*(!isDecodedIntoFile)*/1 && res == 0)
    {

        if ((bluetooth_play) == 0)
        {
            CS4398_Cross_Set(3, 1, 0);//慢速滤波（内部低通滤波）
        }
        if (flacctrl->bps == 24)    //24位音频数据
        {
            if ((bluetooth_play) == 0)
            {
                if (flacctrl->samplerate <= 50000) CS4398_I2S_Cfg(1, 0, 0);//I2S 24位 低速
                else if (flacctrl->samplerate <= 100000) CS4398_I2S_Cfg(1, 0, 1);//I2S 24位高速
                else CS4398_I2S_Cfg(1, 0, 2);//I2S 24位 超高速

                if (flacctrl->samplerate <= 48000) CS4398_Power_Set(0, 0); //根据数据手册的配置，MCLKDIV2设置为0
                else CS4398_Power_Set(0, 1); //根据数据手册的配置，MCLKDIV2设置为1

                if (fc->samplerate < 192000)
                {
                    I2S_DMA_Init(Flac_I2S_DMA_TX_Callback);
                    I2S_Set_Samplerate_and_Bitdepth(flacctrl->samplerate, 24);
                    I2S_DMA_Buffer_Reload(audiodev.i2sbuf1, fc->max_blocksize * 8);
                }
            }
            memset(audiodev.i2sbuf1, 0, fc->max_blocksize * 8);
            memset(audiodev.i2sbuf2, 0, fc->max_blocksize * 8);

        }
        else if (flacctrl->bps == 16)//16位音频数据
        {
            if ((bluetooth_play) == 0)
            {
                if (flacctrl->samplerate <= 50000) CS4398_I2S_Cfg(1, 0, 0);//I2S 24位 低速
                else if (flacctrl->samplerate <= 100000) CS4398_I2S_Cfg(1, 0, 1);//I2S 24位高速
                else CS4398_I2S_Cfg(1, 0, 2);//I2S 24位 超高速
                CS4398_Power_Set(0, 0); //根据数据手册的配置，MCLKDIV2设置为0
                I2S_DMA_Init(Flac_I2S_DMA_TX_Callback);
                I2S_Set_Samplerate_and_Bitdepth(flacctrl->samplerate, 16);
                I2S_DMA_Buffer_Reload(audiodev.i2sbuf1, fc->max_blocksize * 4);
            }

            memset(audiodev.i2sbuf1, 0, fc->max_blocksize * 4);
            memset(audiodev.i2sbuf2, 0, fc->max_blocksize * 4);
        }
        if (res)//不支持的采样率
        {
            free(fc);
            free(flacctrl);
            free(audiodev.i2sbuf1);
            free(audiodev.i2sbuf2);
            free(audiodev.btbuf1);
            free(audiodev.btbuf2);
            audiodev.btbuf1 = NULL;
            audiodev.btbuf2 = NULL;
            audiodev.i2sbuf1 = NULL;
            audiodev.i2sbuf2 = NULL;
            free(audiodev.tbuf);
            free(buffer);
            free(decbuf0);
            free(decbuf1);
            printf("FLAC解码时: 不支持的采样率\n");
            return 1;
        }
        br = fread(buffer, 1, fc->max_framesize, audiodev.file); //读取最大帧长数据
        bytesleft = (int) br;
        fc->decoded0 = (int *) decbuf0;        //解码数组0
        fc->decoded1 = (int *) decbuf1;    //解码数组1
        flac_fptr = ftell(audiodev.file);    //记录当前的文件位置.
        if ((bluetooth_play) == 0) I2S_DMA_Transmit_Start(); //开始一次DMA传输
        printf("states: %02X\n", audiodev.status);
        while (bytesleft)
        {
            if (flac_fptr != ftell(audiodev.file))//说明外部有进行文件快进/快退操作
            {
                if (ftell(audiodev.file) < flacctrl->datastart)//在数据开始之前
                {
                    fseek(audiodev.file, (long) flacctrl->datastart, SEEK_SET); //偏移到数据开始的地方
                }
                br = fread(buffer, 1, fc->max_framesize, audiodev.file); //读取一个最大帧的数据量
                bytesleft = flac_seek_frame(buffer, br, fc);        //查找帧
                if (bytesleft >= 0)                                //找到正确的帧头.
                {
                    fseek(audiodev.file, -(fc->max_framesize) + bytesleft, SEEK_CUR);
                    br = fread(buffer, 1, fc->max_framesize, audiodev.file);
                }
                else
                {
                    printf("flac seek error:%d\r\n", bytesleft);
                    vTaskDelay(1000);
                }
                bytesleft = (int) br;
            }

            if (flac_which_buf == 0)
            {
                p8 = audiodev.i2sbuf1;
            }
            else
            {
                p8 = audiodev.i2sbuf2;
            }

            if (fc->bps == 24)
            {
                res1 = flac_decode_frame24(fc, buffer, bytesleft, (s32 *) p8); //调用24bit位深度的解码函数，直接解码到i2sbuf（省内存）
            }
            else
            {
                res1 = flac_decode_frame16(fc, buffer, bytesleft, (s16 *) p8); //调用16bit位深度的解码函数，直接解码到i2sbuf（省内存）
            }

            if (res1 != 0)//解码出错了
            {
                bytesleft++;
                printf("FLAC解码时: 解码出错\n");
                res = AP_DEC_ERR;
                break;
            }
            else
            {
                FLAC_Fill_Buffer(p8, fc->blocksize, fc->bps);
            }
            consumed = fc->gb.index / 8;
            memmove(buffer, &buffer[consumed], bytesleft - consumed);
            bytesleft -= consumed;

            br = fread(&buffer[bytesleft], 1, fc->max_framesize - bytesleft, audiodev.file);

            if (feof(audiodev.file) != 0)
            {
                res = 1;
            }
            if (res)//读数据出错了
            {
                printf("FLAC解码时: 读取文件出错\n");
                res = AP_ERR;
                break;
            }
            if (br > 0)
            {
                bytesleft += (int) br;
            }
            flac_fptr = ftell(audiodev.file);    //记录当前的文件位置.
            while (audiodev.status & (1 << 1))    //正常播放中
            {
                Flac_Get_Curtime(audiodev.file, flacctrl);//得到总时间和当前播放的时间
                audiodev.totsec = flacctrl->totsec;        //参数传递
                audiodev.cursec = flacctrl->cursec;
                audiodev.bitrate = flacctrl->bitrate;
                audiodev.samplerate = flacctrl->samplerate;
                audiodev.bps = flacctrl->bps;
                if (audiodev.status & 0X01)//没有按下暂停
                {
                    break;
                }
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
            if ((audiodev.status & (1 << 1)) == 0)        //请求结束播放/播放完成
            {
                break;
            }
            flaccount++;
//            printf("flac解码进度: %d\n当前时间: %ld\n总时间: %ld\n\n", flaccount, audiodev.cursec, audiodev.totsec);
        }
        if (bluetooth_play == 0)
        {
            CS4398_Power_Set(1, 0);
            I2S_DMA_Transmit_Stop(); //停止播放
        }
    }
    else
    {
        printf("FLAC解码时: 解码内存申请错误\n");
    }
    audiodev.file_type = FILE_NONE;
    res = AP_ERR;
    flaccount = 0;
    fclose(audiodev.file);
    free(fc);
    free(flacctrl);
    free(audiodev.i2sbuf1);
    free(audiodev.i2sbuf2);
    free(audiodev.btbuf1);
    free(audiodev.btbuf2);
    audiodev.btbuf1 = NULL;
    audiodev.btbuf2 = NULL;
    audiodev.i2sbuf1 = NULL;
    audiodev.i2sbuf2 = NULL;
    free(audiodev.bt_tempbuf);
    free(audiodev.bt_sendbuf);
    audiodev.bt_tempbuf = NULL;
    audiodev.bt_sendbuf = NULL;
    free(buffer);
    free(decbuf0);
    free(decbuf1);
    printf("FLAC结束播放\n");
    return res;
} 

	









































