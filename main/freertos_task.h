//
// Created by Misaka on 24-10-18.
//

#ifndef ST7789_FREERTOS_TASK_H
#define ST7789_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


extern SemaphoreHandle_t semphore_handle;
extern SemaphoreHandle_t bluetooth_send_semphore;
extern SemaphoreHandle_t MutexSemaphore;
extern SemaphoreHandle_t audio_task_create_semphore;

extern QueueHandle_t lv_widgets_delete_queue;

extern TaskHandle_t audio_start_task_handler;
extern void Audio_Play_Start_Task(void *arg);

/*
 *	Audio_Play_Task 任务 配置
 */
#define AUDIO_PLAY_PRIO         	20
#define AUDIO_PLAY_STACK_SIZE   	10240
extern TaskHandle_t  	audio_play_task_handler;

extern void Audio_Play_Task( void * pvParameters );

/*
 *	Gui_Display_Task 任务 配置
 */
#define GUI_DISPLAY_PRIO         	8
#define GUI_DISPLAY_SIZE   	        5120
extern TaskHandle_t  	gui_display_task_handler;

extern void Gui_Display_Task( void * pvParameters );

/*
 *	Key_Detect_Task 任务 配置
 */
#define KEY_UP_PIN                  GPIO_NUM_34
#define KEY_DOWN_PIN                GPIO_NUM_35
#define KEY_LEFT_PIN                GPIO_NUM_36
#define KEY_RIGHT_PIN               GPIO_NUM_39
#define KEY_CENTER_PIN               GPIO_NUM_32

#define KEY_DETECT_PRIO         	5
#define KEY_DETECT_SIZE   	        4096
extern TaskHandle_t    key_task_handler;

extern void Key_Detect_Task( void * pvParameters );

/*
 *	Lv_Widgets_Delete_Task 任务 配置
 */

#define WIDGETS_DELETE_PRIO         	10
#define WIDGETS_DELETE_SIZE   	        2048
extern TaskHandle_t lv_widgets_delete_task_handler;

extern void Lv_Widgets_Delete_Task(void * pvParameters);

#endif //ST7789_FREERTOS_TASK_H
