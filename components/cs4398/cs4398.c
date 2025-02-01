//
// Created by Misaka on 25-1-14.
//

#include "cs4398.h"

#include <stdio.h>
#include <math.h>
#include <soc/i2s_struct.h>
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "soc/i2s_reg.h"
#include "hal/i2s_hal.h"
#include "freertos_task.h"
#include "audioplay.h"

static i2c_master_bus_handle_t i2c_bus_handle;
static i2c_master_dev_handle_t i2c_cs4398_handle;

static i2s_chan_handle_t tx_handle = NULL;

static intr_handle_t i2s_intr;

DMANode DMA_list[15] = {0}; //DMA链表，预创建15个DMA节点（96KHz/24bit时,FLAC每帧为4096 * 8字节）
//每个链表可以传输4092字节数据

i2s_hal_context_t hi2s;

/*
 * I2S DMA初始化函数，用于初始化DMA寄存器、开启中断及注册回调函数
 */
void I2S_DMA_Init(void (*callback)(void *))
{
    i2s_hal_tx_disable_dma(&hi2s); //关闭I2S DMA
    i2s_hal_tx_reset_dma(&hi2s); //重置I2S DMA
    i2s_hal_clear_intr_status(&hi2s, I2S_LL_TX_EVENT_MASK); //清除I2S中断状态
    esp_intr_free(i2s_intr); //释放I2S中断
    esp_intr_alloc_intrstatus(ETS_I2S0_INTR_SOURCE, (ESP_INTR_FLAG_LEVEL3),
                              (uint32_t) i2s_ll_get_interrupt_status_reg(&I2S0), /*(1UL << 11)*/I2S_LL_TX_EVENT_MASK,
                              callback, NULL, &i2s_intr); //分配I2S中断及设置中断回调函数
}

/*
 * I2S DMA缓冲区重载函数，用于重载DMA链表
 * buf: 缓冲区指针
 * size: 缓冲区大小
 */
void I2S_DMA_Buffer_Reload(const uint8_t *buf, uint32_t size)
{
    uint32_t byte_remained = size;
    uint8_t DMAlist_idx = 0;
    memset(DMA_list, 0, sizeof(DMANode) * 15); //清空所有DMA链表
    while (byte_remained > 4092) //4092字节为一个DMA节点的最大传输量
    {
        DMA_list[DMAlist_idx].dw0 |= (1 << 31); //允许的操作者为 DMA 控制器
        DMA_list[DMAlist_idx].dw0 &= ~(0 << 30); //非最后一个节点
        DMA_list[DMAlist_idx].dw0 |= ((uint32_t) (4092) << 12); //传输字节数
        DMA_list[DMAlist_idx].dw0 |= ((uint32_t) (4092) << 0); //传输字节数
        DMA_list[DMAlist_idx].buffer_ptr = (uint32_t) (DMAlist_idx * 4092 + buf); //缓冲区指针
        DMA_list[DMAlist_idx].next_desc_ptr = (uint32_t) &DMA_list[DMAlist_idx + 1]; //下一个DMA节点指针
        byte_remained -= 4092; //剩余字节数
        ++DMAlist_idx; //DMA链表索引加1
        if (DMAlist_idx > 14)
        {
            printf("DMA链表溢出!\n");
            return;
        }
    }
    DMA_list[DMAlist_idx].dw0 |= (1 << 31); //允许的操作者为 DMA 控制器
    DMA_list[DMAlist_idx].dw0 |= (1 << 30); //最后一个节点
    DMA_list[DMAlist_idx].dw0 |= ((uint32_t) (byte_remained) << 12);
    DMA_list[DMAlist_idx].dw0 |= ((uint32_t) (byte_remained) << 0);
    DMA_list[DMAlist_idx].buffer_ptr = (uint32_t) (DMAlist_idx * 4092 + buf);
    DMA_list[DMAlist_idx].next_desc_ptr = 0;
}

/*
 * I2S DMA传输开始函数，用于启动DMA传输
 */
void I2S_DMA_Transmit_Start(void)
{
    i2s_hal_tx_start_link(&hi2s, (uint32_t) &DMA_list);
    i2s_hal_tx_enable_dma(&hi2s);
    i2s_hal_tx_start(&hi2s);
}

/*
 * I2S DMA传输停止函数，用于停止DMA传输
 */
void I2S_DMA_Transmit_Stop(void)
{
    i2s_hal_tx_stop_link(&hi2s);
    i2s_hal_tx_disable_dma(&hi2s);
    i2s_hal_tx_stop(&hi2s);
}

/*
 * I2S HAL句柄初始化函数，用于初始化I2S HAL句柄
 */
void I2S_Init(void)
{
    i2s_hal_init(&hi2s, 0);
}

/*
 * I2S采样率及位深度设置函数，用于设置I2S采样率及位深度
 * samplerate: 采样率
 * bitdepth: 位深度
 */
void I2S_Set_Samplerate_and_Bitdepth(uint32_t samplerate, uint8_t bitdepth)
{
    i2s_del_channel(tx_handle);

    i2s_chan_config_t chan_cfg = {
            .id = 0,
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = 2, //随便设置一个小值，因为不使用API创建的DMA内存
            .dma_frame_num = 16, //随便设置一个小值，因为不使用API创建的DMA内存
            .auto_clear_after_cb = false,
            .auto_clear_before_cb = false,
            .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
    i2s_std_config_t std_cfg = {
            .gpio_cfg = {
                    .mclk = GPIO_NUM_0,
                    .bclk = GPIO_NUM_22,
                    .ws = GPIO_NUM_21,
                    .dout = GPIO_NUM_33,
                    .din = -1,
                    .invert_flags = {
                            .mclk_inv = false,
                            .bclk_inv = false,
                            .ws_inv = false,
                    },
            },
    };
    std_cfg.clk_cfg.sample_rate_hz = samplerate;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_APLL;

    if (bitdepth == 16)
    {
        std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
        std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
        std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
        std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
        std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
        std_cfg.slot_cfg.ws_pol = false;
        std_cfg.slot_cfg.bit_shift = true;
        std_cfg.slot_cfg.msb_right = true;
        if (samplerate > 176000)
        {
            std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_128 / 2; // 未知原因，可能是IDF的BUG，MCLK分频默认会高一倍，故除以2
        }
        else
        {
            std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256 / 2; // 未知原因，可能是IDF的BUG，MCLK分频默认会高一倍，故除以2
        }
    }
    else if (bitdepth == 24) //24位时，IDF开发指南要求分频系数必须为3的倍数
    {
        std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_24BIT;
        std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
        std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
        std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
        std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_24BIT;
        std_cfg.slot_cfg.ws_pol = false;
        std_cfg.slot_cfg.bit_shift = true;
        std_cfg.slot_cfg.msb_right = false;
        if (samplerate > 176000)
        {
            std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_192 / 2; // 未知原因，可能是IDF的BUG，MCLK分频默认会高一倍，故除以2
        }
        else
        {
            std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384 / 2; // 未知原因，可能是IDF的BUG，MCLK分频默认会高一倍，故除以2
        }
    }
    else
    {
        printf("未知的位深度\n");
        return;
    }
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    i2s_hal_tx_reset_fifo(&hi2s);
    i2s_hal_tx_enable_intr(&hi2s);
}

static uint8_t CS4398_Write_Reg(uint8_t reg, uint8_t val)
{
    uint8_t sendbuf[2] = {0};
    sendbuf[0] = reg;
    sendbuf[1] = val;
    ESP_ERROR_CHECK(i2c_master_transmit(i2c_cs4398_handle, sendbuf, 2, 100));
    return 0;
}

static uint8_t CS4398_Read_Reg(uint8_t reg)
{
    uint8_t temp = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(i2c_cs4398_handle, &reg, 1, &temp, 1, 100));
    return temp;
}

/*
 * CS4398初始化函数，用于初始化CS4398
 */
uint8_t CS4398_Init(void)
{
    uint8_t ID = 0;
    i2c_master_bus_config_t i2c_bus_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = -1,
            .scl_io_num = GPIO_NUM_19,
            .sda_io_num = GPIO_NUM_5,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,

    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

    i2c_device_config_t i2c_dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CS4398_ADDR,
            .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &i2c_dev_cfg, &i2c_cs4398_handle));

    ID = CS4398_Read_Reg(1);//读取ID=0x72
    //以下为通用设置
    CS4398_Cross_Set(3, 1, 0);//慢速滤波（内部低通滤波）
    CS4398_Mute_Set(0, 0, 0, 0, 0);
    CS4398_Channel_Cfg(1, 1);
    printf("CS4398 ID:%02X\n", ID);
    return ID;
}


//DIF:PCM模式下0,LSB(左对齐);1:飞利浦标准I2S2 24bit    2-5:MSB(右对齐);
//DSD模式下：0，DSD64+4xMCLK；1，DSD64+6xMCLK；2，DSD64+8xMCLK；3，DSD64+12xMCLK；
//4，DSD128+2xMCLK；5，DSD128+3xMCLK；6，DSD128+4xMCLK；7，DSD128+6xMCLK；
//DE：1，44.1khz去加重；2，48khz去加重；3，32khz去加重；
//FM：0，30-50khz采用率；1，50-100khz采用率；2，100-200khz采用率，3，DSD模式
void CS4398_I2S_Cfg(uint8_t dif, uint8_t de, uint8_t fm)
{
    dif &= 0X07;//限定范围
    CS4398_Write_Reg(0x02, (dif << 4) | (de << 2) | fm);    //R3,CS4398工作模式设置
}

//VOLBA：0左右音量单独控制，1一起控制
//ATA：混音，默认0x09立体声；
//0全部静音，1左静音，2右静音
void CS4398_Channel_Cfg(uint8_t volba, uint8_t ata)
{
    if (ata == 0)
        CS4398_Write_Reg(0x03, volba << 7 | 0);    //
    else
        CS4398_Write_Reg(0x03, volba << 7 | 0x09);    //
}

//CS4398 DSD配置
//pmen：DSD相位调制使能，pmmode=0，128fs相位调制，=1 64fs相位调制
void CS4398_DSD_Cfg(uint8_t staticd, uint8_t invalid, uint8_t pmmode, uint8_t pmen)
{
    CS4398_Write_Reg(0x09, staticd << 3 | invalid << 2 | pmmode << 1 | pmen);
}

//电源设置 0:开机；1:掉电
//div:主MCLK  2分频设置
void CS4398_Power_Set(uint8_t power, uint8_t div)
{
    CS4398_Write_Reg(0x08, power << 7 | 1 << 6 | div << 4);
//    if (power == 1)
//        HAL_GPIO_WritePin(DAC_RST_PORT, DAC_RST_PIN, 0);
}

//pamute=1:pcm数字静音
//damute=1：dsd数字静音
//mutec=1：AB声道同时静音
//mutea=0:正常  =1：A通道静音
//muteb=0:正常  =1：B通道静音
//mutep：静音极性检测
void CS4398_Mute_Set(uint8_t pamute, uint8_t damute, uint8_t mutec, uint8_t mutea, uint8_t muteb)//静音控制
{
    uint8_t reg = 0;
    reg |= pamute << 7 | damute << 6 | mutec << 5 | mutea << 4 | mutea << 3 | muteb << 0;
    CS4398_Write_Reg(0x04, reg);
}

//设置耳机左右声道音量
//voll:左声道音量(0~100)
//volr:右声道音量(0~100)
void CS4398_HPvol_Set(uint8_t vol)
{
    float voll = 0;
    if (vol > 100)vol = 100;//限定范围
    voll = vol;
    voll = sqrtf(voll) * 25.5f;
    voll = 255 - voll;
    CS4398_Write_Reg(0x05, (uint8_t) voll);            //R6,耳机左声道音量设置
    CS4398_Write_Reg(0x06, (uint8_t) voll);    //R7,耳机右声道音量设置,同步更新(HPVU=1)
}

//零点检测+滤波
//szc=0:快速变化，=1：零点检测 =2：软件平滑=3：软件平衡+零点检测
//filt=0:内部滤波器快速滤波；=1慢速滤波
//dir=0：内部正常解码流程可控制音量和滤波 =1：直解码音量不可以控制
void CS4398_Cross_Set(uint8_t szc, uint8_t filt, uint8_t dir)
{
    CS4398_Write_Reg(0x07, szc << 6 | 2 << 4 | filt << 2 | dir << 0);
}
