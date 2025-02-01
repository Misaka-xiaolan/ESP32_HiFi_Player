/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "bt_app_core.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "mp3play.h"
#include "audioplay.h"
#include "freertos_task.h"
#include "flacplay.h"
#include "bluetooth.h"
#include "ui.h"

/* log tags */
#define BT_AV_TAG             "BT_AV"
#define BT_RC_CT_TAG          "RC_CT"

/* device name */
#define TARGET_DEVICE_NAME    "WH-1000XM5"
char target_device_name[50] = {0};
#define LOCAL_DEVICE_NAME     "ESP_A2DP_SRC"

/* AVRCP used transaction label */
#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_RN_VOLUME_CHANGE    (1)



/*********************************
 * STATIC FUNCTION DECLARATIONS
 ********************************/

/* handler for bluetooth stack enabled events */
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param);

/* avrc controller event handler */
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);

/* GAP callback function */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

/* callback function for A2DP source */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/* callback function for A2DP source audio data stream */
static int32_t bt_app_a2d_data_cb(uint8_t *data, int32_t len);

/* callback function for AVRCP controller */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/* handler for heart beat timer */
static void bt_app_a2d_heart_beat(TimerHandle_t arg);

/* A2DP application state machine */
static void bt_app_av_sm_hdlr(uint16_t event, void *param);

/* utils for transfer BLuetooth Deveice Address into string form */
static char *bda2str(esp_bd_addr_t bda, char *str, size_t size);

/* A2DP application state machine handler for each state */
static void bt_app_av_state_unconnected_hdlr(uint16_t event, void *param);

static void bt_app_av_state_connecting_hdlr(uint16_t event, void *param);

static void bt_app_av_state_connected_hdlr(uint16_t event, void *param);

static void bt_app_av_state_disconnecting_hdlr(uint16_t event, void *param);

/*********************************
 * STATIC VARIABLE DEFINITIONS
 ********************************/

static esp_bd_addr_t s_peer_bda = {0};                        /* Bluetooth Device Address of peer device*/
static uint8_t s_peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];  /* Bluetooth Device Name of peer device*/
static volatile int s_a2d_state = APP_AV_STATE_IDLE;                   /* A2DP global state */
static volatile int s_media_state = APP_AV_MEDIA_STATE_IDLE;           /* sub states of APP_AV_STATE_CONNECTED */
static int s_intv_cnt = 0;                                    /* count of heart beat intervals */
static int s_connecting_intv = 0;                             /* count of heart beat intervals for connecting */
static uint32_t s_pkt_cnt = 0;                                /* count of packets */
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;         /* AVRC target notification event capability bit mask */
static TimerHandle_t s_tmr;                                   /* handle of heart beat timer */

BT_DEVICE_LIST bt_device_list[BT_LIST_LEN] = {};
static uint16_t bt_device_num = 0;

void Bluetooth_Connect(void)
{
    esp_a2d_source_connect(s_peer_bda);
}

void Set_s_peer_bda(const esp_bd_addr_t n)
{
    memcpy(s_peer_bda, n, ESP_BD_ADDR_LEN);
}

uint16_t Get_bt_device_num(void)
{
    return bt_device_num;
}

void Set_bt_device_num(uint16_t n)
{
    bt_device_num = n;
}

int Get_A2DP_Global_State(void)
{
    return s_a2d_state;
}

void Set_A2DP_Global_State(int n)
{
    s_a2d_state = n;
}

/*********************************
 * STATIC FUNCTION DEFINITIONS
 ********************************/

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
    {
        return NULL;
    }

    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir)
    {
        return false;
    }

    /* get complete or short local name from eir data */
    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname)
    {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname)
    {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN)
        {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname)
        {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len)
        {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void inquiry_scan_result_organize(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;     /* class of device */
    int32_t rssi = -129;  /* invalid value */
    uint8_t *eir = NULL;
    esp_bt_gap_dev_prop_t *p;
    uint8_t is_in_list = 0;

    /* handle the discovery results */
    ESP_LOGI(BT_AV_TAG, "Scanned device: %s", bda2str(param->disc_res.bda, bda_str, 18));
    for (int i = 0; i < param->disc_res.num_prop; i++)
    {
        p = param->disc_res.prop + i;
        switch (p->type)
        {
            case ESP_BT_GAP_DEV_PROP_COD:
                cod = *(uint32_t *) (p->val);
//                ESP_LOGI(BT_AV_TAG, "--Class of Device: 0x%"PRIx32, cod);
                break;
            case ESP_BT_GAP_DEV_PROP_RSSI:
                rssi = *(int8_t *) (p->val);
//                ESP_LOGI(BT_AV_TAG, "--RSSI: %"PRId32, rssi);
                break;
            case ESP_BT_GAP_DEV_PROP_EIR:
                eir = (uint8_t *) (p->val);
                break;
            case ESP_BT_GAP_DEV_PROP_BDNAME:
            default:
                break;
        }
    }

    /* search for device with MAJOR service class as "rendering" in COD */
//    if (!esp_bt_gap_is_valid_cod(cod) ||
//        !(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING))
//    {
//        return;
//    }

    /* search for target device in its Extended Inqury Response */
    if (eir)
    {
        get_name_from_eir(eir, s_peer_bdname, NULL);
        if (strcmp((char *) s_peer_bdname, target_device_name) == 0 || strcmp((char *) s_peer_bdname, "S6") == 0)
        {
            ESP_LOGI(BT_AV_TAG, "Found a target device, address %s, name %s", bda_str, s_peer_bdname);
            s_a2d_state = APP_AV_STATE_DISCOVERED;
//            memcpy(s_peer_bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
            ESP_LOGI(BT_AV_TAG, "Cancel device discovery ...");
            esp_bt_gap_cancel_discovery();
        }
        printf("find device: %s\n", s_peer_bdname);
        for (uint16_t i = 0; i < BT_LIST_LEN; i++)
        {
            if (strcmp((char *) bda_str, (char *) bt_device_list[i].d_bda_str) == 0 || bda_str[0] == 0 ||
                strcmp((char *) s_peer_bdname, (char *) bt_device_list[i].d_name) == 0)
            {
                is_in_list = 1;
                break;
            }
        }
        if (is_in_list == 0)
        {
            strcpy((char *) bt_device_list[bt_device_num].d_bda_str, (char *) bda_str);
            strcpy((char *) bt_device_list[bt_device_num].d_name, (char *) s_peer_bdname);
            memcpy(bt_device_list[bt_device_num].d_bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
            bt_device_num++;
        }
        if (bt_device_num == BT_LIST_LEN)
        {
            bt_device_num = 0;
        }
    }
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event)
    {
        /* when device discovered a result, this event comes */
        case ESP_BT_GAP_DISC_RES_EVT:
        {
            if (s_a2d_state == APP_AV_STATE_DISCOVERING)
            {
                inquiry_scan_result_organize(param);
            }
            break;
        }
            /* when discovery state changed, this event comes */
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        {
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
            {
                if (s_a2d_state == APP_AV_STATE_DISCOVERED)
                {
                    s_a2d_state = APP_AV_STATE_CONNECTING;
                    ESP_LOGI(BT_AV_TAG, "Device discovery stopped.");
                    ESP_LOGI(BT_AV_TAG, "a2dp connecting to peer: %s", s_peer_bdname);
                    /* connect source to peer device specified by Bluetooth Device Address */
                    esp_a2d_source_connect(s_peer_bda);
                }
//                else
//                {
//                    /* not discovered, continue to discover */
//                    ESP_LOGI(BT_AV_TAG, "Device discovery failed, continue to discover...");
//                    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
//                }
                ESP_LOGI(BT_AV_TAG, "Discovery stopped.");
                printf("total number of devices found: %d\ndevice list:\n", bt_device_num);
                for (uint16_t i = 0; i < bt_device_num; i++)
                {
                    printf("%d:  %s\n", i + 1, bt_device_list[i].d_name);
                    printf("%02X,%02X,%02X,%02X,%02X,%02X\n", bt_device_list[i].d_bda[0], bt_device_list[i].d_bda[1],
                           bt_device_list[i].d_bda[2], bt_device_list[i].d_bda[3], bt_device_list[i].d_bda[4],
                           bt_device_list[i].d_bda[5]);
                }
                Bluetooth_Search_Cplt_Callback();
            }
            else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED)
            {
                ESP_LOGI(BT_AV_TAG, "Discovery started.");
            }
            break;
        }
            /* when authentication completed, this event comes */
        case ESP_BT_GAP_AUTH_CMPL_EVT:
        {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGI(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
                        esp_log_buffer_hex(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            }
            else
            {
                ESP_LOGE(BT_AV_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;
        }
            /* when Legacy Pairing pin code requested, this event comes */
        case ESP_BT_GAP_PIN_REQ_EVT:
        {
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit: %d", param->pin_req.min_16_digit);
            if (param->pin_req.min_16_digit)
            {
                ESP_LOGI(BT_AV_TAG, "Input pin code: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            }
            else
            {
                ESP_LOGI(BT_AV_TAG, "Input pin code: 1234");
                esp_bt_pin_code_t pin_code;
                pin_code[0] = '1';
                pin_code[1] = '2';
                pin_code[2] = '3';
                pin_code[3] = '4';
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }

#if (CONFIG_EXAMPLE_SSP_ENABLED == true)
            /* when Security Simple Pairing user confirmation requested, this event comes */
        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %"PRIu32,
                     param->cfm_req.num_val);
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
            /* when Security Simple Pairing passkey notified, this event comes */
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %"PRIu32, param->key_notif.passkey);
            break;
            /* when Security Simple Pairing passkey requested, this event comes */
        case ESP_BT_GAP_KEY_REQ_EVT:
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
            break;
#endif

            /* when GAP mode changed, this event comes */
        case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
            break;
        case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
            if (param->get_dev_name_cmpl.status == ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT device name: %s", param->get_dev_name_cmpl.name);
            }
            else
            {
                ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT failed, state: %d",
                         param->get_dev_name_cmpl.status);
            }
            break;
            /* other */
        default:
        {
            ESP_LOGI(BT_AV_TAG, "event: %d", event);
            break;
        }
    }

    return;
}

static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    switch (event)
    {
        /* when stack up worked, this event comes */
        case BT_APP_STACK_UP_EVT:
        {
            char *dev_name = LOCAL_DEVICE_NAME;
            esp_bt_gap_set_device_name(dev_name);
            esp_bt_gap_register_callback(bt_app_gap_cb);

            esp_avrc_ct_init();
            esp_avrc_ct_register_callback(bt_app_rc_ct_cb);

            esp_avrc_rn_evt_cap_mask_t evt_set = {0};
            esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
            ESP_ERROR_CHECK(esp_avrc_tg_set_rn_evt_cap(&evt_set));

            esp_a2d_source_init();
            esp_a2d_register_callback(&bt_app_a2d_cb);
            esp_a2d_source_register_data_callback(bt_app_a2d_data_cb);

            /* Avoid the state error of s_a2d_state caused by the connection initiated by the peer device. */
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            esp_bt_gap_get_device_name();

            ESP_LOGI(BT_AV_TAG, "Starting device discovery...");
            s_a2d_state = APP_AV_STATE_DISCOVERING;
//            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 5, 0);
            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 5, 0);
            /* create and start heart beat timer */
            do
            {
                int tmr_id = 0;
                s_tmr = xTimerCreate("connTmr", (10000 / portTICK_PERIOD_MS),
                                     pdTRUE, (void *) &tmr_id, bt_app_a2d_heart_beat);
                xTimerStart(s_tmr, portMAX_DELAY);
            } while (0);
            break;
        }
            /* other */
        default:
        {
            ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
            break;
        }
    }
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    bt_app_work_dispatch(bt_app_av_sm_hdlr, event, param, sizeof(esp_a2d_cb_param_t), NULL);
}

/* generate some random noise to simulate source audio */
static int32_t bt_app_a2d_data_cb(uint8_t *data, int32_t len)
{
    static uint32_t musicbuf_count = 0;
    static uint32_t last_remained = 0;//初始值为0
    static uint8_t is_decode_suspend = 0;
    static uint8_t tempbuf_used = 0;
    uint8_t *doublebuffer_flag = NULL;
    int32_t doublebytes_to_fill = 0;//初始值位2048
    uint32_t doublebytes_resampled = audiodev.bt_outputframes * 2;
    int16_t *music = NULL;

//    uint16_t bt_sendbuf_size = (doublebytes_resampled / 256) * 256;
//    uint16_t bt_tempbuf_size = doublebytes_resampled + 300;
    uint32_t bt_sendbuf_size = 0;
    uint32_t bt_tempbuf_size = 0;
//    xSemaphoreTake(bluetooth_send_semphore, portMAX_DELAY);
    if (data == NULL || len < 0)
    {
//        xSemaphoreGive(bluetooth_send_semphore);
        return len;
    }
    int16_t *p_buf = (int16_t *) data;
    if (audiodev.file_type == FILE_NONE)
    {
//        memset(p_buf, 0, len);
//        xSemaphoreGive(bluetooth_send_semphore);
        return len;
    }

    switch (audiodev.file_type)
    {
        case FILE_NONE:
//            xSemaphoreGive(bluetooth_send_semphore);
            return len;
            break;
        case FILE_MP3:
            if (mp3_which_buf == 0)
            {
                music = audiodev.btbuf2; //mp3_which_buf = 0
            }
            else
            {
                music = audiodev.btbuf1; //mp3_which_buf = 1
            }
            bt_sendbuf_size = 2048;
            bt_tempbuf_size = 2500;
            if (!audiodev.bt_tempbuf || !audiodev.bt_sendbuf)
            {
                audiodev.bt_tempbuf = malloc(bt_tempbuf_size * sizeof(int16_t));
                audiodev.bt_sendbuf = malloc(bt_sendbuf_size * sizeof(int16_t));
            }
//            else
//            {
//                audiodev.bt_tempbuf = realloc(audiodev.bt_tempbuf, bt_tempbuf_size * sizeof(int16_t));
//                audiodev.bt_sendbuf = realloc(audiodev.bt_sendbuf, bt_sendbuf_size * sizeof(int16_t));
//            }
            doublebuffer_flag = (uint8_t *) &mp3_which_buf;
            break;
        case FILE_FLAC_24Bit:
        case FILE_FLAC_16Bit:
            if (flac_which_buf == 0)
            {
                music = audiodev.btbuf2; //flac_which_buf = 0
            }
            else
            {
                music = audiodev.btbuf1; //flac_which_buf = 1
            }
            bt_sendbuf_size = (doublebytes_resampled / 256) * 256;
            bt_tempbuf_size = bt_sendbuf_size + 512;
            if (!audiodev.bt_tempbuf || !audiodev.bt_sendbuf)
            {
                audiodev.bt_tempbuf = malloc(bt_tempbuf_size * sizeof(int16_t));
                audiodev.bt_sendbuf = malloc(bt_sendbuf_size * sizeof(int16_t));
            }
//            else
//            {
//                audiodev.bt_tempbuf = realloc(audiodev.bt_tempbuf, bt_tempbuf_size * sizeof(int16_t));
//                audiodev.bt_sendbuf = realloc(audiodev.bt_sendbuf, bt_sendbuf_size * sizeof(int16_t));
//            }
//            printf("%d, %d\n", bt_sendbuf_size, bt_tempbuf_size);
            doublebuffer_flag = (uint8_t *) &flac_which_buf;
            break;

        case FILE_WAV_16Bit:
            break;
        default:
            break;
    }
    doublebytes_to_fill = bt_sendbuf_size - last_remained;//初始值位2048
    if (!audiodev.bt_tempbuf || !audiodev.bt_sendbuf)
    {
        printf("malloc_failed!!!\n");
        return len;
    }
    if (musicbuf_count == 0)
    {
        if (is_decode_suspend == 0)
        {
            memcpy(audiodev.bt_sendbuf, audiodev.bt_tempbuf, sizeof(int16_t) * last_remained); //将上次没有发送完的数据放入发送缓冲
            memcpy((int16_t *) (audiodev.bt_sendbuf + last_remained), music,
                   sizeof(int16_t) * doublebytes_to_fill); //紧跟着上次未发送的数据，填充新数据
            last_remained = doublebytes_resampled - doublebytes_to_fill; //计算新剩余多少数据
            memcpy(audiodev.bt_tempbuf, (int16_t *) (music + doublebytes_to_fill),
                   sizeof(int16_t) * last_remained); //将剩余的数据填入临时缓冲，以备下次读取
            if (last_remained >= bt_sendbuf_size)// 临时缓冲内足够2048个数据了
            {
                is_decode_suspend = 1; //暂停一次解码，下次用临时缓冲内的数据发送
            }
        }
        else if (is_decode_suspend == 1) // 上次解码暂停了
        {
            memcpy(audiodev.bt_sendbuf, audiodev.bt_tempbuf, sizeof(int16_t) * bt_sendbuf_size); //将临时缓冲内2048个数据送入发送缓冲区
            last_remained = last_remained - bt_sendbuf_size; //计算临时缓冲区剩多少数据
            memcpy(audiodev.bt_tempbuf, (int16_t *) (audiodev.bt_tempbuf + bt_sendbuf_size),
                   sizeof(int16_t) * last_remained); //将剩余的数据移动到临时缓冲的头部
            tempbuf_used = 1;
        }
//        printf("last_remained : %d\n", last_remained);
    }
    for (uint32_t i = 0; i < len / 2; i++)
    {
        p_buf[i] = audiodev.bt_sendbuf[musicbuf_count + i];
    }
    musicbuf_count += len / 2; //对已发送的数据进行计数

    if (musicbuf_count >= bt_sendbuf_size) //sendbuf内的数据全部发送完成
    {
        musicbuf_count = 0;
        if (is_decode_suspend == 0)
        {
            if (doublebuffer_flag != NULL)
            {
                *doublebuffer_flag = !(*doublebuffer_flag); //切换解码缓冲区
            }
            xSemaphoreGive(semphore_handle);
        }
        else
        {
            if (tempbuf_used == 1) //这次用的临时缓冲，下次就应该继续解码了
            {
                if (doublebuffer_flag != NULL)
                {
                    *doublebuffer_flag = !(*doublebuffer_flag); //切换解码缓冲区
                }
                xSemaphoreGive(semphore_handle);
                is_decode_suspend = 0;
                tempbuf_used = 0;
//                printf("decode continued\n");
            }
            else
            {
//                if (audiodev.file_type != FILE_FLAC_16Bit)
//                {
                printf("decode suspended\n");
//                }
            }
        }
//        xSemaphoreGive(bluetooth_send_semphore);
    }

    return len;
}

//static int32_t bt_app_a2d_data_cb(uint8_t *data, int32_t len)
//{
//    static uint16_t musicbuf_count = 0;
//    static uint16_t last_remained = 0;
//    static uint8_t is_decode_suspend = 0;
//    static uint8_t tempbuf_used = 0;
//    uint8_t *doublebuffer_flag = NULL;
//    int16_t doublebytes_to_fill = 0;
//    uint16_t doublebytes_resampled = audiodev.bt_outputframes * 2;
//    int16_t *music = NULL;
//
//    uint16_t bt_sendbuf_size = 0;
//    uint16_t bt_tempbuf_size = 0;
//
//    if (data == NULL || len < 0)
//    {
//        return len;
//    }
//    int16_t *p_buf = (int16_t *) data;
//    if (audiodev.file_type == FILE_NONE)
//    {
//        memset(p_buf, 0, len);
//        return len;
//    }
//    switch (audiodev.file_type)
//    {
//        case FILE_NONE:
//            break;
//        case FILE_MP3:
//            music = (mp3_which_buf == 0) ? audiodev.btbuf2 : audiodev.btbuf1;
//            bt_sendbuf_size = 2048;
//            bt_tempbuf_size = 2500;
//            break;
//        case FILE_FLAC_24Bit:
//        case FILE_FLAC_16Bit:
//            music = (flac_which_buf == 0) ? audiodev.btbuf2 : audiodev.btbuf1;
//            bt_sendbuf_size = (doublebytes_resampled / 256) * 256;
//            bt_tempbuf_size = bt_sendbuf_size + 512;
//            break;
//        case FILE_WAV_16Bit:
//            break;
//        default:
//            break;
//    }
//
//    if (!audiodev.bt_tempbuf || !audiodev.bt_sendbuf)
//    {
//        audiodev.bt_tempbuf = malloc(bt_tempbuf_size * sizeof(int16_t));
//        audiodev.bt_sendbuf = malloc(bt_sendbuf_size * sizeof(int16_t));
//        if (!audiodev.bt_tempbuf || !audiodev.bt_sendbuf)
//        {
//            printf("malloc_failed!!!\n");
//            return len;
//        }
//    }
//
//    doublebytes_to_fill = bt_sendbuf_size - last_remained;
//
//    if (musicbuf_count == 0)
//    {
//        if (is_decode_suspend == 0)
//        {
//            memcpy(audiodev.bt_sendbuf, audiodev.bt_tempbuf, sizeof(int16_t) * last_remained);
//            memcpy(audiodev.bt_sendbuf + last_remained, music, sizeof(int16_t) * doublebytes_to_fill);
//            last_remained = doublebytes_resampled - doublebytes_to_fill;
//            memcpy(audiodev.bt_tempbuf, music + doublebytes_to_fill, sizeof(int16_t) * last_remained);
//            if (last_remained >= bt_sendbuf_size)
//            {
//                is_decode_suspend = 1;
//            }
//        }
//        else
//        {
//            memcpy(audiodev.bt_sendbuf, audiodev.bt_tempbuf, sizeof(int16_t) * bt_sendbuf_size);
//            last_remained -= bt_sendbuf_size;
//            memmove(audiodev.bt_tempbuf, audiodev.bt_tempbuf + bt_sendbuf_size, sizeof(int16_t) * last_remained);
//            tempbuf_used = 1;
//        }
//    }
//
//    memcpy(p_buf, audiodev.bt_sendbuf + musicbuf_count, len);
//    musicbuf_count += len / 2;
//
//    if (musicbuf_count >= bt_sendbuf_size)
//    {
//        musicbuf_count = 0;
//        if (is_decode_suspend == 0)
//        {
//            if (doublebuffer_flag != NULL)
//            {
//                *doublebuffer_flag = !(*doublebuffer_flag);
//            }
//            xSemaphoreGive(semphore_handle);
//        }
//        else if (tempbuf_used == 1)
//        {
//            if (doublebuffer_flag != NULL)
//            {
//                *doublebuffer_flag = !(*doublebuffer_flag);
//            }
//            xSemaphoreGive(semphore_handle);
//            is_decode_suspend = 0;
//            tempbuf_used = 0;
//        }
//        else
//        {
//            printf("decode suspended\n");
//        }
//    }
//    return len;
//}

static void bt_app_a2d_heart_beat(TimerHandle_t arg)
{
    bt_app_work_dispatch(bt_app_av_sm_hdlr, BT_APP_HEART_BEAT_EVT, NULL, 0, NULL);
}

static void bt_app_av_sm_hdlr(uint16_t event, void *param)
{
    ESP_LOGI(BT_AV_TAG, "%s state: %d, event: 0x%x", __func__, s_a2d_state, event);

    /* select handler according to different states */
    switch (s_a2d_state)
    {
        case APP_AV_STATE_DISCOVERING:
        case APP_AV_STATE_DISCOVERED:
            break;
        case APP_AV_STATE_UNCONNECTED:
            bt_app_av_state_unconnected_hdlr(event, param);
            break;
        case APP_AV_STATE_CONNECTING:
            bt_app_av_state_connecting_hdlr(event, param);
            break;
        case APP_AV_STATE_CONNECTED:
            bt_app_av_state_connected_hdlr(event, param);
            break;
        case APP_AV_STATE_DISCONNECTING:
            bt_app_av_state_disconnecting_hdlr(event, param);
            break;
        default:
            ESP_LOGE(BT_AV_TAG, "%s invalid state: %d", __func__, s_a2d_state);
            break;
    }
}

static void bt_app_av_state_unconnected_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;
    /* handle the events of interest in unconnected state */
    switch (event)
    {
        case ESP_A2D_CONNECTION_STATE_EVT:
        case ESP_A2D_AUDIO_STATE_EVT:
        case ESP_A2D_AUDIO_CFG_EVT:
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            break;
        case BT_APP_HEART_BEAT_EVT:
        {
            uint8_t *bda = s_peer_bda;
            ESP_LOGI(BT_AV_TAG, "a2dp connecting to peer: %02x:%02x:%02x:%02x:%02x:%02x",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            esp_a2d_source_connect(s_peer_bda);
            s_a2d_state = APP_AV_STATE_CONNECTING;
            s_connecting_intv = 0;
            break;
        }
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            ESP_LOGI(BT_AV_TAG, "%s, delay value: %u * 1/10 ms", __func__,
                     a2d->a2d_report_delay_value_stat.delay_value);
            break;
        }
        default:
        {
            ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
            break;
        }
    }
}

static void bt_app_av_state_connecting_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in connecting state */
    switch (event)
    {
        case ESP_A2D_CONNECTION_STATE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
            {
                ESP_LOGI(BT_AV_TAG, "a2dp connected");
                s_a2d_state = APP_AV_STATE_CONNECTED;
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                Bluetooth_Connected_Callback();
            }
            else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
            {
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT:
        case ESP_A2D_AUDIO_CFG_EVT:
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            break;
        case BT_APP_HEART_BEAT_EVT:
            /**
             * Switch state to APP_AV_STATE_UNCONNECTED
             * when connecting lasts more than 2 heart beat intervals.
             */
//            if (++s_connecting_intv >= 2)
//            {
//                s_a2d_state = APP_AV_STATE_UNCONNECTED;
//                s_connecting_intv = 0;
//            }
            break;
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            ESP_LOGI(BT_AV_TAG, "%s, delay value: %u * 1/10 ms", __func__,
                     a2d->a2d_report_delay_value_stat.delay_value);
            break;
        }
        default:
            ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
            break;
    }
}

static void bt_app_av_media_proc(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    switch (s_media_state)
    {
        case APP_AV_MEDIA_STATE_IDLE:
        {
            if (event == BT_APP_HEART_BEAT_EVT)
            {
                ESP_LOGI(BT_AV_TAG, "a2dp media ready checking ...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            }
            else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
            {
                a2d = (esp_a2d_cb_param_t *) (param);
                if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
                    a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
                {
                    ESP_LOGI(BT_AV_TAG, "a2dp media ready");
                    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                    s_media_state = APP_AV_MEDIA_STATE_STARTING;
                }
            }
            break;
        }
        case APP_AV_MEDIA_STATE_STARTING:
        {
            if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
            {
                a2d = (esp_a2d_cb_param_t *) (param);
                if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START &&
                    a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
                {
                    ESP_LOGI(BT_AV_TAG, "a2dp media start successfully.");
                    s_intv_cnt = 0;
                    s_media_state = APP_AV_MEDIA_STATE_STARTED;
                }
                else
                {
                    /* not started successfully, transfer to idle state */
                    ESP_LOGI(BT_AV_TAG, "a2dp media start failed.");
                    s_media_state = APP_AV_MEDIA_STATE_IDLE;
                }
            }
            break;
        }
        case APP_AV_MEDIA_STATE_STARTED:
        {
            if (event == BT_APP_HEART_BEAT_EVT)
            {
                /* stop media after 10 heart beat intervals */
//            if (++s_intv_cnt >= 10) {
//                ESP_LOGI(BT_AV_TAG, "a2dp media suspending...");
//                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
//                s_media_state = APP_AV_MEDIA_STATE_STOPPING;
//                s_intv_cnt = 0;
//            }
            }
            break;
        }
        case APP_AV_MEDIA_STATE_STOPPING:
        {
            if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
            {
                a2d = (esp_a2d_cb_param_t *) (param);
                if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND &&
                    a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
                {
                    ESP_LOGI(BT_AV_TAG, "a2dp media suspend successfully, disconnecting...");
                    s_media_state = APP_AV_MEDIA_STATE_IDLE;
                    esp_a2d_source_disconnect(s_peer_bda);
                    s_a2d_state = APP_AV_STATE_DISCONNECTING;
                }
                else
                {
                    ESP_LOGI(BT_AV_TAG, "a2dp media suspending...");
                    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
                }
            }
            break;
        }
        default:
        {
            break;
        }
    }
}

static void bt_app_av_state_connected_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in connected state */
    switch (event)
    {
        case ESP_A2D_CONNECTION_STATE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
            {
                ESP_LOGI(BT_AV_TAG, "a2dp disconnected");
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state)
            {
                s_pkt_cnt = 0;
            }
            break;
        }
        case ESP_A2D_AUDIO_CFG_EVT:
            // not supposed to occur for A2DP source
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        case BT_APP_HEART_BEAT_EVT:
        {
            bt_app_av_media_proc(event, param);
            break;
        }
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            ESP_LOGI(BT_AV_TAG, "%s, delay value: %u * 1/10 ms", __func__,
                     a2d->a2d_report_delay_value_stat.delay_value);
            break;
        }
        default:
        {
            ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
            break;
        }
    }
}

static void bt_app_av_state_disconnecting_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in disconnecing state */
    switch (event)
    {
        case ESP_A2D_CONNECTION_STATE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
            {
                ESP_LOGI(BT_AV_TAG, "a2dp disconnected");
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT:
        case ESP_A2D_AUDIO_CFG_EVT:
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        case BT_APP_HEART_BEAT_EVT:
            break;
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
        {
            a2d = (esp_a2d_cb_param_t *) (param);
            ESP_LOGI(BT_AV_TAG, "%s, delay value: 0x%u * 1/10 ms", __func__,
                     a2d->a2d_report_delay_value_stat.delay_value);
            break;
        }
        default:
        {
            ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
            break;
        }
    }
}

/* callback function for AVRCP controller */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event)
    {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        case ESP_AVRC_CT_METADATA_RSP_EVT:
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
        {
            bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
            break;
        }
        default:
        {
            ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
            break;
        }
    }
}

static void bt_av_volume_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_VOLUME_CHANGE))
    {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_VOLUME_CHANGE, ESP_AVRC_RN_VOLUME_CHANGE, 0);
    }
}

void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    switch (event_id)
    {
        /* when volume changed locally on target, this event comes */
        case ESP_AVRC_RN_VOLUME_CHANGE:
        {
            ESP_LOGI(BT_RC_CT_TAG, "Volume changed: %d", event_parameter->volume);
            ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume: volume %d", event_parameter->volume);
            esp_avrc_ct_send_set_absolute_volume_cmd(APP_RC_CT_TL_RN_VOLUME_CHANGE, event_parameter->volume);
            bt_av_volume_changed();
            break;
        }
            /* other */
        default:
            break;
    }
}

/* AVRC controller event handler */
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *) (p_param);

    switch (event)
    {
        /* when connection state changed, this event comes */
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        {
            uint8_t *bda = rc->conn_stat.remote_bda;
            ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

            if (rc->conn_stat.connected)
            {
                esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
            }
            else
            {
                s_avrc_peer_rn_cap.bits = 0;
            }
            break;
        }
            /* when passthrough responded, this event comes */
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        {
            ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough response: key_code 0x%x, key_state %d, rsp_code %d",
                     rc->psth_rsp.key_code,
                     rc->psth_rsp.key_state, rc->psth_rsp.rsp_code);
            break;
        }
            /* when metadata responded, this event comes */
        case ESP_AVRC_CT_METADATA_RSP_EVT:
        {
            ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata response: attribute id 0x%x, %s", rc->meta_rsp.attr_id,
                     rc->meta_rsp.attr_text);
            free(rc->meta_rsp.attr_text);
            break;
        }
            /* when notification changed, this event comes */
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        {
            ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
            bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
            break;
        }
            /* when indicate feature of remote device, this event comes */
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        {
            ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %"PRIx32", TG features %x", rc->rmt_feats.feat_mask,
                     rc->rmt_feats.tg_feat_flag);
            break;
        }
            /* when get supported notification events capability of peer device, this event comes */
        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        {
            ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                     rc->get_rn_caps_rsp.evt_set.bits);
            s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;

            bt_av_volume_changed();
            break;
        }
            /* when set absolute volume responded, this event comes */
        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
        {
            ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume response: volume %d", rc->set_volume_rsp.volume);
            break;
        }
            /* other */
        default:
        {
            ESP_LOGE(BT_RC_CT_TAG, "%s unhandled event: %d", __func__, event);
            break;
        }
    }
}

/*********************************
 * MAIN ENTRY POINT
 ********************************/

void bluetooth_app_main(void)
{
    char bda_str[18] = {0};
    esp_err_t ret;
    /* initialize NVS — it is used to store PHY calibration data */
    //已在开机时初始化完成

    /*
     * This example only uses the functions of Classical Bluetooth.
     * So release the controller memory for Bluetooth Low Energy.
     */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    int temp;
    if ((temp = esp_bt_controller_init(&bt_cfg)) != ESP_OK)
    {
        ESP_LOGE(BT_AV_TAG, "%s initialize controller failed %d", __func__, temp);
        return;
    }

    if ((temp = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK)
    {
        ESP_LOGE(BT_AV_TAG, "%s enable controller failed %d", __func__, temp);
        return;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
#if (CONFIG_EXAMPLE_SSP_ENABLED == false)
    bluedroid_cfg.ssp_en = false;
#endif
    if ((ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK)
    {
        ESP_LOGE(BT_AV_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if (esp_bluedroid_enable() != ESP_OK)
    {
        ESP_LOGE(BT_AV_TAG, "%s enable bluedroid failed", __func__);
        return;
    }

#if (CONFIG_EXAMPLE_SSP_ENABLED == true)
    /* set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    ESP_LOGI(BT_AV_TAG, "Own address:[%s]", bda2str((uint8_t *) esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    bt_app_task_start_up();
    /* Bluetooth device name, connection mode and profile set up */
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_STACK_UP_EVT, NULL, 0, NULL);
}

/*    Misaka自定义函数     */

uint8_t Get_s_media_state(void)
{
    return s_media_state;
}

void Bt_Play_Control(uint8_t state)
{
    if (state)
    {
        if (s_media_state == APP_AV_MEDIA_STATE_READY)
        {
            printf("Starting playing...\n");
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            s_media_state = APP_AV_MEDIA_STATE_STARTING;
//            while (s_media_state != APP_AV_MEDIA_STATE_STARTED)
//            {
//                if (s_media_state == APP_AV_MEDIA_STATE_READY)
//                {
//                    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
//                    s_media_state = APP_AV_MEDIA_STATE_STARTING;
//                }
//                vTaskDelay(1000);
//            }
        }
        else if (s_media_state == APP_AV_MEDIA_STATE_STARTED)
        {
//            printf("Playing has been started\n");
            return;
        }
        else
        {
            printf("Media device is not ready! Can't start playing\n");
            return;
        }
    }
    else
    {
        if (s_media_state == APP_AV_MEDIA_STATE_STARTED)
        {
            printf("Stop playing...\n");
            s_media_state = APP_AV_MEDIA_STATE_IDLE;
            free(audiodev.bt_tempbuf);
            free(audiodev.bt_sendbuf);
            audiodev.bt_tempbuf = NULL;
            audiodev.bt_sendbuf = NULL;
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
        }
        else if (s_media_state == APP_AV_MEDIA_STATE_READY || s_media_state == APP_AV_MEDIA_STATE_IDLE)
        {
//            printf("Playing has been stopped\n");
            return;
        }
        else
        {
            printf("Media device is not ready! Can't stop playing\n");
            return;
        }
    }
}






