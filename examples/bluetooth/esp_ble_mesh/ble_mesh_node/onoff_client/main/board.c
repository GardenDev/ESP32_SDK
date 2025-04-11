/* board.c - Board-specific hooks */

/*
 * Copyright (c) 2017 Intel Corporation
 * Additional Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"

#include "iot_button.h"
#include "board.h"

#define TAG "BOARD"

#define BUTTON_IO_1             1
#define BUTTON_IO_2             2
#define BUTTON_IO_3             0
#define BUTTON_IO_4             20
#define BUTTON_ACTIVE_LEVEL     0
#define MAX_PRESS_INTERVAL      3000  // 最長允許 5 次按鍵總時間 (ms)

extern bool example_ble_mesh_send_gen_onoff_set(uint8_t model_idx, uint8_t pin);
extern void example_ble_mesh_send_scene_recall(uint8_t model_idx, uint8_t pin);
extern void resetBleMeshProvision(void);

static int press_count = 0;
static int64_t first_press_time = 0;
static bool trigger_reset = false;

struct _led_state led_state[5] = {
    { LED_OFF, LED_OFF, LED_1, "LED_1"  },
    { LED_OFF, LED_OFF, LED_2, "LED_2"  },
    { LED_OFF, LED_OFF, LED_3, "LED_3"  },
    { LED_OFF, LED_OFF, LED_4, "LED_4"  },
    { LED_OFF, LED_OFF, LED_0, "LED_BG" },
};

void board_led_operation(uint8_t pin, uint8_t onoff)
{
    for (int i = 0; i < ARRAY_SIZE(led_state); i++) {
        if (led_state[i].pin != pin) {
            continue;
        }
        if (onoff == led_state[i].previous) {
            ESP_LOGW(TAG, "led %s is already %s",
                led_state[i].name, (onoff ? "on" : "off"));
            return;
        }
        gpio_set_level(pin, onoff);
        led_state[i].previous = onoff;
        return;
    }
    ESP_LOGE(TAG, "LED is not found!");
}

static void board_led_init(void)
{
    for (int i = 0; i < ARRAY_SIZE(led_state); i++) {
        gpio_reset_pin(led_state[i].pin);
        gpio_set_direction(led_state[i].pin, GPIO_MODE_OUTPUT);
        gpio_set_level(led_state[i].pin, LED_OFF);
        led_state[i].previous = LED_OFF;
    }
}

static void button_push_cb(void* arg)
{
    ESP_LOGI(TAG, "push cb (%d)", (uint8_t)arg);
    static int64_t last_press_time = 0;
    int64_t now = esp_timer_get_time();  // 單位為 microseconds

    if (now - last_press_time < 200000) return;  // 200ms debounce
    last_press_time = now;

    if (press_count == 0) {
        first_press_time = now;
    }

    press_count++;

    if (press_count == 5) {
        if ((now - first_press_time) <= MAX_PRESS_INTERVAL * 1000) {
            trigger_reset = true;
        }
        press_count = 0;
    }

    // 超時重置（可移到 timer loop 判斷更精準）
    if ((now - first_press_time) > MAX_PRESS_INTERVAL * 1000) {
        press_count = 1;
        first_press_time = now;
        trigger_reset = false;
    }

    ESP_LOGI(TAG, "press_count (%d)", press_count);
}

static void button_tap_cb(void* arg)
{
    ESP_LOGI(TAG, "tap cb (%d)", (uint8_t)arg);

    uint8_t idx = (uint8_t)arg;

    example_ble_mesh_send_gen_onoff_set(idx, led_state[idx].pin);
    example_ble_mesh_send_scene_recall(idx, led_state[idx].pin);
}

static void button_long_press_cb(void* arg)
{
    ESP_LOGI(TAG, "long press cb (%s)", (char *)arg);
    if (trigger_reset) {
        resetBleMeshProvision();
        trigger_reset = false;
    }
}

static void board_button_init(void)
{
    button_handle_t btn_handle_0 = iot_button_create(BUTTON_IO_1, BUTTON_ACTIVE_LEVEL);
    if (btn_handle_0) {
        iot_button_set_evt_cb(btn_handle_0, BUTTON_CB_PUSH, button_push_cb, 0);
        iot_button_set_evt_cb(btn_handle_0, BUTTON_CB_RELEASE, button_tap_cb, 0);
        iot_button_set_evt_cb(btn_handle_0, BUTTON_CB_SERIAL, button_long_press_cb, "RESET");
    }

    button_handle_t btn_handle_1 = iot_button_create(BUTTON_IO_2, BUTTON_ACTIVE_LEVEL);
    if (btn_handle_1) {
        iot_button_set_evt_cb(btn_handle_1, BUTTON_CB_RELEASE, button_tap_cb, 1);
    }

    button_handle_t btn_handle_2 = iot_button_create(BUTTON_IO_3, BUTTON_ACTIVE_LEVEL);
    if (btn_handle_2) {
        iot_button_set_evt_cb(btn_handle_2, BUTTON_CB_RELEASE, button_tap_cb, 2);
    }

    button_handle_t btn_handle_3 = iot_button_create(BUTTON_IO_4, BUTTON_ACTIVE_LEVEL);
    if (btn_handle_3) {
        iot_button_set_evt_cb(btn_handle_3, BUTTON_CB_RELEASE, button_tap_cb, 3);
    }
}

void board_init(void)
{
    board_led_init();
    board_button_init();
}
