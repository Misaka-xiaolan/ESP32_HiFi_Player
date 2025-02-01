//
// Created by Misaka on 25-1-14.
//

#ifndef ST7789_CS4398_H
#define ST7789_CS4398_H


#include <stdint.h>
#include <hal/i2s_hal.h>

#define CS4398_ADDR                0X4C    //CS4398的器件地址,固定为0X4C，但是根据STM32的规则，应为0x98

#define EQ1_80Hz        0X00
#define EQ1_105Hz        0X01
#define EQ1_135Hz        0X02
#define EQ1_175Hz        0X03

#define EQ2_230Hz        0X00
#define EQ2_300Hz        0X01
#define EQ2_385Hz        0X02
#define EQ2_500Hz        0X03

#define EQ3_650Hz        0X00
#define EQ3_850Hz        0X01
#define EQ3_1100Hz        0X02
#define EQ3_14000Hz        0X03

#define EQ4_1800Hz        0X00
#define EQ4_2400Hz        0X01
#define EQ4_3200Hz        0X02
#define EQ4_4100Hz        0X03

#define EQ5_5300Hz        0X00
#define EQ5_6900Hz        0X01
#define EQ5_9000Hz        0X02
#define EQ5_11700Hz        0X03

typedef struct
{
    uint32_t dw0;
    uint32_t buffer_ptr;
    uint32_t next_desc_ptr;
} DMANode;

extern i2s_hal_context_t hi2s;
extern DMANode DMA_first_list;
extern DMANode DMA_second_list;
extern DMANode DMA_list[15];

extern uint8_t CS4398_Init(void);

extern void CS4398_I2S_Cfg(uint8_t dif, uint8_t de, uint8_t fm);

extern void CS4398_Channel_Cfg(uint8_t volba, uint8_t ata);

extern void CS4398_DSD_Cfg(uint8_t staticd, uint8_t invalid, uint8_t pmmode, uint8_t pmen);

extern void CS4398_Power_Set(uint8_t power, uint8_t div);

extern void CS4398_Mute_Set(uint8_t pamute, uint8_t damute, uint8_t mutec, uint8_t mutea, uint8_t muteb);

extern void CS4398_HPvol_Set(uint8_t vol);

extern void CS4398_Cross_Set(uint8_t szc, uint8_t filt, uint8_t dir);

extern void I2S_Init(void);

extern void I2S_DMA_Init(void (*callback)(void *));

extern void I2S_Set_Samplerate_and_Bitdepth(uint32_t samplerate, uint8_t bitdepth);

extern void I2S_DMA_Buffer_Reload(const uint8_t *buf, uint32_t size);

extern void I2S_DMA_Transmit_Start(void);

extern void I2S_DMA_Transmit_Stop(void);


#endif //ST7789_CS4398_H

