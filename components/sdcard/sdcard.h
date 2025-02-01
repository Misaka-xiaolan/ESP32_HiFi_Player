//
// Created by Misaka on 24-10-18.
//

#ifndef ST7789_SDCARD_H
#define ST7789_SDCARD_H

#define MOUNT_POINT     "/sdcard"

#define T_BIN        0X00    //bin文件
#define T_LRC        0X10    //lrc文件
#define T_NES        0X20    //nes文件

#define T_TEXT        0X30    //.txt文件
#define T_C            0X31    //.c文件
#define T_H            0X32    //.h文件

#define T_WAV        0X40    //WAV文件
#define T_MP3        0X41    //MP3文件
#define T_APE        0X42    //APE文件
#define T_FLAC    0X43    //FLAC文件
#define T_FLA      0X44    //FLAC文件
#define T_DFF        0X45
#define T_DSF        0X46

#define T_BMP        0X50    //bmp文件
#define T_JPG        0X51    //jpg文件
#define T_JPEG        0X52    //jpeg文件
#define T_GIF        0X53    //gif文件

#define T_AVI        0X60    //avi文件

esp_err_t SDCard_Fatfs_Init(void);

void SDCard_RW_Test(void);

int Dir_List_File(char *dirpath);

uint8_t f_typetell(char *fname);

uint64_t Get_File_Size(char *fname);

#endif //ST7789_SDCARD_H
