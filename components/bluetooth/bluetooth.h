//
// Created by Misaka on 24-11-16.
//

#ifndef ST7789_BLUETOOTH_H
#define ST7789_BLUETOOTH_H

#include "esp_gap_bt_api.h"

#define BT_LIST_LEN     30

extern void bluetooth_app_main(void);

extern void Bt_Play_Control(uint8_t state);

extern uint8_t Get_s_media_state(void);

extern uint16_t Get_bt_device_num(void);

extern void Set_bt_device_num(uint16_t n);

extern int Get_A2DP_Global_State(void);

extern void Set_A2DP_Global_State(int n);

extern void Set_s_peer_bda(const esp_bd_addr_t n);

extern void Bluetooth_Connect(void);

typedef struct
{
    esp_bd_addr_t d_bda;
    uint8_t d_bda_str[18];
    uint8_t d_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} BT_DEVICE_LIST;

extern BT_DEVICE_LIST bt_device_list[BT_LIST_LEN];
extern char target_device_name[50];

enum
{
    BT_APP_STACK_UP_EVT = 0x0000,    /* event for stack up */
    BT_APP_HEART_BEAT_EVT = 0xff00,    /* event for heart beat */
};

/* A2DP global states */
enum
{
    APP_AV_STATE_IDLE,
    APP_AV_STATE_DISCOVERING,
    APP_AV_STATE_DISCOVERED,
    APP_AV_STATE_UNCONNECTED,
    APP_AV_STATE_CONNECTING,
    APP_AV_STATE_CONNECTED,
    APP_AV_STATE_DISCONNECTING,
};

/* sub states of APP_AV_STATE_CONNECTED */
enum
{
    APP_AV_MEDIA_STATE_IDLE,
    APP_AV_MEDIA_STATE_READY,
    APP_AV_MEDIA_STATE_STARTING,
    APP_AV_MEDIA_STATE_STARTED,
    APP_AV_MEDIA_STATE_STOPPING,
};


#endif //ST7789_BLUETOOTH_H
