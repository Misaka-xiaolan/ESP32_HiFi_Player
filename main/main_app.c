#include <stdio.h>
#include <nvs_flash.h>
#include <esp_pm.h>
#include "sdcard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"
#include "audioplay.h"
#include "freertos_task.h"
#include "cs4398.h"

uint16_t total_num = 0;
uint32_t freesize;
uint32_t totalsize;

void app_main(void)
{
    esp_pm_config_t pm_config = {
            .max_freq_mhz = 240,
            .min_freq_mhz = 40,
            .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    semphore_handle = xSemaphoreCreateBinary();
    bluetooth_send_semphore = xSemaphoreCreateBinary();
    MutexSemaphore = xSemaphoreCreateMutex();
    audio_task_create_semphore = xSemaphoreCreateBinary();
    lv_widgets_delete_queue = xQueueCreate(1, sizeof(lv_obj_t *));

    xSemaphoreGive(bluetooth_send_semphore);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        printf("NVS ERROR\n");
    }
    ESP_ERROR_CHECK(ret);

    SDCard_Fatfs_Init();
    SDCard_RW_Test();
    totalsize = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    freesize = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    printf("堆总空间: %lu\n剩余空间: %lu\n", totalsize, freesize);
    CS4398_Init();
    CS4398_HPvol_Set(35);
    I2S_Init();

    xTaskCreate(Gui_Display_Task, "gui", GUI_DISPLAY_SIZE, NULL, GUI_DISPLAY_PRIO, &gui_display_task_handler);
    xTaskCreate(Lv_Widgets_Delete_Task, "lv_widgets_del", WIDGETS_DELETE_SIZE, NULL, WIDGETS_DELETE_PRIO,
                &lv_widgets_delete_task_handler);
    Audio_Play();
    return;
}




