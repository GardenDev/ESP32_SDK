// Copyright 2016-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/rtc_io_struct.h"
#include "soc/sens_reg.h"
#include "soc/sens_struct.h"
#include "driver/temp_sensor.h"
#include "regi2c_ctrl.h"
#include "esp_log.h"
#include "esp32s2/esp_efuse_rtc_table.h"

static const char *TAG = "tsens";

#define TSENS_CHECK(res, ret_val) ({                                    \
    if (!(res)) {                                                       \
        ESP_LOGE(TAG, "%s(%d)", __FUNCTION__, __LINE__);                \
        return (ret_val);                                               \
    }                                                                   \
})
#define TSENS_XPD_WAIT_DEFAULT 0xFF   /* Set wait cycle time(8MHz) from power up to reset enable. */
#define TSENS_ADC_FACTOR  (0.4386)
#define TSENS_DAC_FACTOR  (27.88)
#define TSENS_SYS_OFFSET  (20.52)

typedef struct {
    int index;
    int offset;
    int set_val;
    int range_min;
    int range_max;
    int error_max;
} tsens_dac_offset_t;

static const tsens_dac_offset_t dac_offset[TSENS_DAC_MAX] = {
    /*     DAC     Offset reg_val  min  max  error */
    {TSENS_DAC_L0,   -2,     5,    50,  125,   3},
    {TSENS_DAC_L1,   -1,     7,    20,  100,   2},
    {TSENS_DAC_L2,    0,    15,   -10,   80,   1},
    {TSENS_DAC_L3,    1,    11,   -30,   50,   2},
    {TSENS_DAC_L4,    2,    10,   -40,   20,   3},
};

static SemaphoreHandle_t rtc_tsens_mux = NULL;

static float s_deltaT = NAN; // Unused number

esp_err_t temp_sensor_set_config(temp_sensor_config_t tsens)
{
    CLEAR_PERI_REG_MASK(RTC_CNTL_ANA_CONF_REG, RTC_CNTL_SAR_I2C_FORCE_PD_M);
    SET_PERI_REG_MASK(RTC_CNTL_ANA_CONF_REG, RTC_CNTL_SAR_I2C_FORCE_PU_M);
    CLEAR_PERI_REG_MASK(ANA_CONFIG_REG, I2C_SAR_M);
    SET_PERI_REG_MASK(ANA_CONFIG2_REG, ANA_SAR_CFG2_M);
    REGI2C_WRITE_MASK(I2C_SAR_ADC, I2C_SARADC_TSENS_DAC, dac_offset[tsens.dac_offset].set_val);
    SENS.sar_tctrl.tsens_clk_div = tsens.clk_div;
    SENS.sar_tctrl.tsens_power_up_force = 1;
    SENS.sar_tctrl2.tsens_xpd_wait = TSENS_XPD_WAIT_DEFAULT;
    SENS.sar_tctrl2.tsens_xpd_force = 1;
    SENS.sar_tctrl2.tsens_reset = 1;// Reset the temp sensor.
    SENS.sar_tctrl2.tsens_reset = 0;// Clear the reset status.
    ESP_LOGI(TAG, "Config temperature range [%d°C ~ %d°C], error < %d°C",
             dac_offset[tsens.dac_offset].range_min,
             dac_offset[tsens.dac_offset].range_max,
             dac_offset[tsens.dac_offset].error_max);
    return ESP_OK;
}

esp_err_t temp_sensor_get_config(temp_sensor_config_t *tsens)
{
    TSENS_CHECK(tsens != NULL, ESP_ERR_INVALID_ARG);
    CLEAR_PERI_REG_MASK(RTC_CNTL_ANA_CONF_REG, RTC_CNTL_SAR_I2C_FORCE_PD_M);
    SET_PERI_REG_MASK(RTC_CNTL_ANA_CONF_REG, RTC_CNTL_SAR_I2C_FORCE_PU_M);
    CLEAR_PERI_REG_MASK(ANA_CONFIG_REG, I2C_SAR_M);
    SET_PERI_REG_MASK(ANA_CONFIG2_REG, ANA_SAR_CFG2_M);
    tsens->dac_offset = REGI2C_READ_MASK(I2C_SAR_ADC, I2C_SARADC_TSENS_DAC);
    for (int i = TSENS_DAC_L0; i < TSENS_DAC_MAX; i++) {
        if ((int)tsens->dac_offset == dac_offset[i].set_val) {
            tsens->dac_offset = dac_offset[i].index;
            break;
        }
    }
    tsens->clk_div = SENS.sar_tctrl.tsens_clk_div;
    return ESP_OK;
}

esp_err_t temp_sensor_start(void)
{
    if (rtc_tsens_mux == NULL) {
        rtc_tsens_mux = xSemaphoreCreateMutex();
    }
    TSENS_CHECK(rtc_tsens_mux != NULL, ESP_ERR_NO_MEM);
    SENS.sar_tctrl.tsens_dump_out = 0;
    SENS.sar_tctrl2.tsens_clkgate_en = 1;
    SENS.sar_tctrl.tsens_power_up = 1;
    return ESP_OK;
}

esp_err_t temp_sensor_stop(void)
{
    SENS.sar_tctrl.tsens_power_up = 0;
    SENS.sar_tctrl2.tsens_clkgate_en = 0;
    if (rtc_tsens_mux != NULL) {
        vSemaphoreDelete(rtc_tsens_mux);
        rtc_tsens_mux = NULL;
    }
    return ESP_OK;
}

esp_err_t temp_sensor_read_raw(uint32_t *tsens_out)
{
    TSENS_CHECK(tsens_out != NULL, ESP_ERR_INVALID_ARG);
    TSENS_CHECK(rtc_tsens_mux != NULL, ESP_ERR_INVALID_STATE);
    xSemaphoreTake(rtc_tsens_mux, portMAX_DELAY);
    SENS.sar_tctrl.tsens_dump_out = 1;
    while (!SENS.sar_tctrl.tsens_ready);
    *tsens_out = SENS.sar_tctrl.tsens_out;
    SENS.sar_tctrl.tsens_dump_out = 0;
    xSemaphoreGive(rtc_tsens_mux);
    return ESP_OK;
}

static void read_delta_t_from_efuse(void)
{
    uint32_t version = esp_efuse_rtc_table_read_calib_version();
    if (version == 1 || version == 2) {
        // fetch calibration value for temp sensor from eFuse
        s_deltaT = esp_efuse_rtc_table_get_parsed_efuse_value(RTCCALIB_IDX_TMPSENSOR, false) / 10.0;
    } else {
        // no value to fetch, use 0.
        s_deltaT = 0;
    }
    ESP_LOGD(TAG, "s_deltaT = %f\n", s_deltaT);
}

static float parse_temp_sensor_raw_value(uint32_t tsens_raw, const int dac_offset)
{
    if (isnan(s_deltaT)) { //suggests that the value is not initialized
        read_delta_t_from_efuse();
    }
    float result = (TSENS_ADC_FACTOR * (float)tsens_raw - TSENS_DAC_FACTOR * dac_offset - TSENS_SYS_OFFSET) - s_deltaT;
    return result;
}

esp_err_t temp_sensor_read_celsius(float *celsius)
{
    TSENS_CHECK(celsius != NULL, ESP_ERR_INVALID_ARG);
    temp_sensor_config_t tsens;
    uint32_t tsens_out = 0;
    esp_err_t ret = temp_sensor_get_config(&tsens);
    if (ret == ESP_OK) {
        ret = temp_sensor_read_raw(&tsens_out);
        TSENS_CHECK(ret == ESP_OK, ret);
        const tsens_dac_offset_t *dac = &dac_offset[tsens.dac_offset];
        *celsius = parse_temp_sensor_raw_value(tsens_out, dac->offset);
        if (*celsius < dac->range_min || *celsius > dac->range_max) {
            ESP_LOGW(TAG, "Exceeding the temperature range!");
            ret = ESP_ERR_INVALID_STATE;
        }
    }
    return ret;
}
