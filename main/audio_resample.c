//
// Created by Misaka on 24-11-17.
//

#include <stdio.h>
#include "audio_resample.h"

void PCM_Chunk_Resample(const int16_t *input, long input_frames, int16_t *output, long *output_frames, double src_ratio)
{
    float *input_float = (float *) malloc(input_frames * 2 * sizeof(float)); // 双声道，乘以2
    if (!input_float || !output)
    {
        printf("Memory allocation failed\n");
        return;
    }
    for (long i = 0; i < input_frames * 2; i++)
    {
        input_float[i] = (float) input[i] / 32768.0f; // 将 int16_t 转换为归一化的 float
    }
    long max_output_frames = (long) (input_frames * src_ratio + 1);
    float *output_float = (float *) malloc(max_output_frames * 2 * sizeof(float)); // 双声道，乘以2
    if (!output_float)
    {
        printf("Memory allocation failed\n");
        free(input_float);
        return;
    }
    SRC_DATA src_data;
    src_data.data_in = input_float;
    src_data.input_frames = input_frames;
    src_data.data_out = output_float;
    src_data.output_frames = max_output_frames; // 预分配的输出缓冲区大小
    src_data.src_ratio = src_ratio;
    src_data.end_of_input = 0;

    int error = src_simple(&src_data, SRC_ZERO_ORDER_HOLD, 2);
    if (error)
    {
        printf("Error during resampling: %s\n", src_strerror(error));
        free(input_float);
        free(output_float);
        return;
    }
    for (long i = 0; i < src_data.output_frames_gen * 2; i++)
    { // 双声道，乘以2
        output[i] = (int16_t) (output_float[i] * 32767); // 归一化浮点值转回整数
    }

    // 释放临时缓冲区
    free(input_float);
    free(output_float);
}



