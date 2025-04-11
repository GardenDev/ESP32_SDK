/* main.c - Application main entry point */

/*
 * Copyright (c) 2017 Intel Corporation
 * Additional Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_time_scene_model_api.h"

#include "board.h"
#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#define TAG "EXAMPLE"

#define CID_ESP       0x02E5
#define BUTTON_AMOUNT 1

typedef struct {
    int blink_delay_ms;
} task_param_t;

task_param_t led_param = {
    .blink_delay_ms = 500
};

static uint8_t dev_uuid[16] = { 0xdd, 0xdd };
static TaskHandle_t breath_task = NULL;

static struct onoff_info_store {
    uint16_t net_idx;   /* NetKey Index */
    uint16_t app_idx;   /* AppKey Index */
    uint8_t  onoff[BUTTON_AMOUNT];     /* Remote OnOff */
    uint8_t  tid[BUTTON_AMOUNT];       /* Message TID */
} __attribute__((packed)) onoff_store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .onoff = {LED_OFF},
    .tid = {0x0},
};

static struct scene_info_store {
    uint16_t net_idx;   /* NetKey Index */
    uint16_t app_idx;   /* AppKey Index */
    uint8_t  tid;       /* Message TID */
} __attribute__((packed)) scene_store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .tid = 0x0,
};

static nvs_handle_t NVS_HANDLE;
static const char * NVS_ONOFF_KEY = "onoff_client";
static const char * NVS_SCENE_KEY = "scene_client";

static esp_ble_mesh_client_t onoff_client[BUTTON_AMOUNT];
static esp_ble_mesh_client_t scene_client[BUTTON_AMOUNT];

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub_0, 2 + 1, ROLE_NODE);

ESP_BLE_MESH_MODEL_PUB_DEFINE(scene_cli_pub_0, 2 + 3, ROLE_NODE);

static esp_ble_mesh_model_t models_0[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub_0, &onoff_client[0]),
    ESP_BLE_MESH_MODEL_SCENE_CLI(&scene_cli_pub_0, &scene_client[0]),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, models_0, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
#if 0
    .output_size = 4,
    .output_actions = ESP_BLE_MESH_DISPLAY_NUMBER,
    .input_actions = ESP_BLE_MESH_PUSH,
    .input_size = 4,
#else
    .output_size = 0,
    .output_actions = 0,
#endif
};

static void mesh_onoff_info_store(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_ONOFF_KEY, &onoff_store, sizeof(onoff_store));
}

static void mesh_scene_info_store(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_SCENE_KEY, &scene_store, sizeof(scene_store));
}

static void mesh_example_info_restore(void)
{
    esp_err_t err = ESP_OK;
    bool exist = false;

    err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_ONOFF_KEY, &onoff_store, sizeof(onoff_store), &exist);
    if (err != ESP_OK) {
        return;
    }

    if (exist) {
        ESP_LOGI(TAG, "Restore, net_idx 0x%04x, app_idx 0x%04x, onoff %u, tid 0x%02x",
            onoff_store.net_idx, onoff_store.app_idx, onoff_store.onoff, onoff_store.tid);
    }
    exist = false;
    err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_SCENE_KEY, &scene_store, sizeof(scene_store), &exist);
    if (err != ESP_OK) {
        return;
    }

    if (exist) {
        ESP_LOGI(TAG, "Restore, net_idx 0x%04x, app_idx 0x%04x, tid 0x%02x",
            scene_store.net_idx, scene_store.app_idx, scene_store.tid);
    }
}

void light_breathing(void *pvParameter)
{
    task_param_t *param = (task_param_t *)pvParameter;
    while(1) {
        board_led_operation(LED_0, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(param->blink_delay_ms));
        board_led_operation(LED_0, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(param->blink_delay_ms));
    }
    vTaskDelete(NULL);
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);
    vTaskDelete(breath_task);
    breath_task = NULL;
    board_led_operation(LED_0, LED_ON);
    onoff_store.net_idx = net_idx;
    scene_store.net_idx = net_idx;
    /* mesh_example_info_store() shall not be invoked here, because if the device
     * is restarted and goes into a provisioned state, then the following events
     * will come:
     * 1st: ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT
     * 2nd: ESP_BLE_MESH_PROV_REGISTER_COMP_EVT
     * So the store.net_idx will be updated here, and if we store the mesh example
     * info here, the wrong app_idx (initialized with 0xFFFF) will be stored in nvs
     * just before restoring it.
     */
}

void resetBleMeshProvision() {
    ble_mesh_nvs_erase(NVS_HANDLE, NVS_ONOFF_KEY);
    ble_mesh_nvs_erase(NVS_HANDLE, NVS_SCENE_KEY);
    ESP_ERROR_CHECK(esp_ble_mesh_node_local_reset());
    ESP_ERROR_CHECK(esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        mesh_example_info_restore(); /* Restore proper mesh example info */
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        led_param.blink_delay_ms = 100; 
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        led_param.blink_delay_ms = 500;
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        resetBleMeshProvision();
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

bool example_ble_mesh_send_gen_onoff_set(uint8_t model_idx, uint8_t led_pin)
{
    esp_ble_mesh_generic_client_set_state_t set = {0};
    esp_ble_mesh_client_common_param_t common = {0};
    esp_err_t err = ESP_OK;

    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
    common.model = onoff_client[model_idx].model;
    common.ctx.net_idx = onoff_store.net_idx;
    common.ctx.app_idx = onoff_store.app_idx;
    common.ctx.addr = common.model->pub->publish_addr;   /* to all nodes */
    common.ctx.send_ttl = 3;
    common.ctx.send_rel = false;
    common.msg_timeout = 0;     /* 0 indicates that timeout value from menuconfig will be used */
    common.msg_role = ROLE_NODE;

    set.onoff_set.op_en = false;
    set.onoff_set.onoff = onoff_store.onoff[model_idx];
    set.onoff_set.tid = onoff_store.tid[model_idx];

    err = esp_ble_mesh_generic_client_set_state(&common, &set);
    if (err) {
        ESP_LOGE(TAG, "Send Generic OnOff Set Unack failed");
    } else {
        onoff_store.tid[model_idx]++;
        board_led_operation(led_pin, onoff_store.onoff[model_idx]);
        onoff_store.onoff[model_idx] = !onoff_store.onoff[model_idx];
        mesh_onoff_info_store(); /* Store proper mesh example info */
    }
    return set.onoff_set.onoff;
}

void example_ble_mesh_send_scene_recall(uint8_t model_idx, uint8_t led_pin)
{
    esp_ble_mesh_time_scene_client_set_state_t set = {0};
    esp_ble_mesh_client_common_param_t common = {0};
    esp_err_t err = ESP_OK;

    common.opcode = ESP_BLE_MESH_MODEL_OP_SCENE_RECALL_UNACK;
    common.model = scene_client[model_idx].model;
    common.ctx.net_idx = scene_store.net_idx;
    common.ctx.app_idx = scene_store.app_idx;
    common.ctx.addr = common.model->pub->publish_addr;   /* to all nodes */
    common.ctx.send_ttl = 3;
    common.ctx.send_rel = false;
    common.msg_timeout = 0;     /* 0 indicates that timeout value from menuconfig will be used */
    common.msg_role = ROLE_NODE;

    set.scene_recall.op_en = false;
    set.scene_recall.scene_number = model_idx + 1;
    set.scene_recall.tid = scene_store.tid;

    err = esp_ble_mesh_time_scene_client_set_state(&common, &set);
    if (err) {
        ESP_LOGE(TAG, "err: %d, Send Time Scene 1 Set Unack failed", err);
    } else {
        scene_store.tid++;
        board_led_operation(led_pin, true);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        board_led_operation(led_pin, false);
    }
    return;
}

static void example_ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                               esp_ble_mesh_generic_client_cb_param_t *param)
{
    ESP_LOGI(TAG, "Generic client, event %u, error code %d, opcode is 0x%04x",
        event, param->error_code, param->params->opcode);

    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT");
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, onoff %d", param->status_cb.onoff_status.present_onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT");
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff %d", param->status_cb.onoff_status.present_onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT");
        /*if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            // If failed to get the response of Generic OnOff Set, resend Generic OnOff Set
            example_ble_mesh_send_gen_onoff_set(LED_1);
        }*/
        break;
    default:
        break;
    }
}

static void example_ble_mesh_scene_client_cb(esp_ble_mesh_time_scene_client_cb_event_t event,
                                             esp_ble_mesh_time_scene_client_cb_param_t *param)
{
    ESP_LOGI(TAG, "Scene client, event %u, error code %d, opcode is 0x%04x",
        event, param->error_code, param->params->opcode);

    switch (event) {
    case ESP_BLE_MESH_TIME_SCENE_CLIENT_GET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_TIME_SCENE_CLIENT_GET_STATE_EVT");
        break;
    case ESP_BLE_MESH_TIME_SCENE_CLIENT_SET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_TIME_SCENE_CLIENT_SET_STATE_EVT");
        break;
    case ESP_BLE_MESH_TIME_SCENE_CLIENT_PUBLISH_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_TIME_SCENE_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_TIME_SCENE_CLIENT_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_TIME_SCENE_CLIENT_TIMEOUT_EVT");
        /*if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_SCENE_RECALL) {
            // If failed to get the response of Generic OnOff Set, resend Generic OnOff Set
            example_ble_mesh_send_scene_recall(LED_1, 1);
        }*/
        break;
    default:
        break;
    }
}

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            if (param->value.state_change.mod_app_bind.company_id == 0xFFFF) {
                if (param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
                    onoff_store.app_idx = param->value.state_change.mod_app_bind.app_idx;
                    mesh_onoff_info_store(); /* Store proper mesh example info */
                } else if (param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_SCENE_CLI) {
                    scene_store.app_idx = param->value.state_change.mod_app_bind.app_idx;
                    mesh_scene_info_store(); /* Store proper mesh example info */
                }
            }
            break;
        default:
            break;
        }
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err = ESP_OK;

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);
    esp_ble_mesh_register_time_scene_client_callback(example_ble_mesh_scene_client_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized");

    uint8_t mac[6];
    esp_base_mac_addr_get(mac);
    char ble_mesh_name[32] = {0};
    sprintf(ble_mesh_name, "%d-key Switch-%02X%02X%02X%02X%02X%02X", BUTTON_AMOUNT, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_ble_mesh_set_unprovisioned_device_name(ble_mesh_name);

    if (esp_ble_mesh_node_is_provisioned()) {
        ESP_LOGW(TAG, "node already provisioned");
    } else {
        ESP_LOGW(TAG, "node not provisioned");
    }

    return err;
}

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    board_init();

    xTaskCreate(light_breathing, "breath", 2048, &led_param, 5, &breath_task);

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    /* Open nvs namespace for storing/restoring mesh example info */
    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err) {
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }
}
