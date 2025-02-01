//
// Created by Misaka on 24-11-17.
//

#ifndef ST7789_AUDIO_RESAMPLE_H
#define ST7789_AUDIO_RESAMPLE_H
#include "samplerate.h"

extern void PCM_Chunk_Resample(const int16_t *input, long input_frames, int16_t *output, long *output_frames, double src_ratio);

#endif //ST7789_AUDIO_RESAMPLE_H
