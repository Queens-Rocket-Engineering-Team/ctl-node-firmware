#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "ads112c04.h"
#include "ads112c04_defines.h"
#include "ads112c04_internal.h"

static const char *TAG = "ADS112C04";

// initialize the adc on the i2c bus
static esp_err_t s_init_i2c(ads112c04_t *ads112c04, i2c_master_bus_handle_t bus_handle, uint32_t i2c_frequency) {
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ads112c04->addr,
        .scl_speed_hz = i2c_frequency,
    };

    // initialize device and fill dev_handle
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(bus_handle, &dev_cfg, &ads112c04->dev_handle), TAG, "Add device to master bus failed"
    );

    return ESP_OK;
}

// ISR handler for ADC DRDY pins
static void IRAM_ATTR drdy_isr_handler(void *arg) {
    ads112c04_t *ads112c04 = (ads112c04_t *)arg;

    BaseType_t higher_priority_task_woken = pdFALSE;
    // indicate conversion is ready
    xSemaphoreGiveFromISR(ads112c04->semaphore_DRDY, &higher_priority_task_woken);

    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

// initialize the ads112c04 device
esp_err_t ads112c04_init(ads112c04_t *ads112c04, const ads112c04_config_t *ads112c04_cfg) {
    if (ads112c04 == NULL || ads112c04_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ads112c04->addr = ads112c04_cfg->addr;
    ESP_RETURN_ON_ERROR(
        s_init_i2c(ads112c04, ads112c04_cfg->bus_handle, ads112c04_cfg->bus_frequency), TAG, "I2C init failed"
    );

    // initialize mutex
    ads112c04->mutex = xSemaphoreCreateMutexStatic(&ads112c04->mutex_buffer);
    configASSERT(ads112c04->mutex);

    // initialize conversion binary semaphore
    ads112c04->semaphore_DRDY = xSemaphoreCreateBinaryStatic(&ads112c04->semaphore_buffer_DRDY);
    configASSERT(ads112c04->semaphore_DRDY);

    ESP_RETURN_ON_ERROR(gpio_reset_pin(ads112c04_cfg->drdy_pin), TAG, "Failed to reset GPIO for DRDY");

    // set up DRDY pin ISR handler
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ads112c04_cfg->drdy_pin),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO config for DRDY failed");

    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(ads112c04_cfg->drdy_pin, drdy_isr_handler, (void *)ads112c04),
        TAG,
        "Failed to add GPIO to ISR handler"
    );

    ads112c04_internal_send_command(ads112c04, RESET);
    vTaskDelay(pdTICKS_TO_MS(5));

    // set default configuration values
    uint8_t reg1 = 0;
    reg1 |= (0x06 << 5) & DR_MASK; // set to 1000SPS
    reg1 |= (0x01 << 4) & MODE_MASK;
    reg1 |= (0x00 << 3) & CM_MASK;   // set to single shot mode
    reg1 |= (0x02 << 1) & VREF_MASK; // set ref voltage to AVDD-AVSS
    reg1 |= 0x00 & TS_MASK;          // disable temperature sensor mode
    ESP_RETURN_ON_ERROR(ads112c04_internal_write_register(ads112c04, 1, reg1), TAG, "Failed to write reg1");

    // set default ADC settings
    ads112c04->ref_voltage = 5; // give the ability to set this in config later
    ads112c04->gain = 1;
    ads112c04->pga_enabled = false;
    ads112c04->conversion_mode = CM_SINGLE_SHOT;
    return ESP_OK;
}

uint8_t ads112c04_get_address(const ads112c04_t *ads112c04) {
    return ads112c04->addr;
}

bool ads112c04_is_gain_valid(uint8_t gain) {
    if (ads112c04_internal_get_gain_bits(gain) == INVALID_GAIN) {
        return false;
    }
    return true;
}

bool ads112c04_is_mux_valid(ads112c04_pin_t p_pin, ads112c04_pin_t n_pin) {
    if (ads112c04_internal_get_mux_code(p_pin, n_pin) == INVALID_MUX) {
        return false;
    }
    return true;
}

esp_err_t ads112c04_set_inputs(
    ads112c04_t *ads112c04,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ads112c04_internal_lock_adc(ads112c04), TAG, "Failed to lock adc");
    esp_err_t ret = ads112c04_internal_set_inputs(ads112c04, p_pin, n_pin, gain, pga_enabled);
    ESP_RETURN_ON_ERROR(ads112c04_internal_unlock_adc(ads112c04), TAG, "Failed to unlock adc");
    return ret;
}

esp_err_t ads112c04_set_single_shot(ads112c04_t *ads112c04) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ads112c04_internal_lock_adc(ads112c04), TAG, "Failed to lock adc");
    esp_err_t ret = ads112c04_internal_set_single_shot(ads112c04);
    ESP_RETURN_ON_ERROR(ads112c04_internal_unlock_adc(ads112c04), TAG, "Failed to unlock adc");
    return ret;
}

esp_err_t ads112c04_set_idac_current(ads112c04_t *ads112c04, idac_current_t current) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ads112c04_internal_lock_adc(ads112c04), TAG, "Failed to lock adc");
    esp_err_t ret = ads112c04_internal_set_idac_current(ads112c04, current);
    ESP_RETURN_ON_ERROR(ads112c04_internal_unlock_adc(ads112c04), TAG, "Failed to unlock adc");
    return ret;
}

esp_err_t ads112c04_set_idac_routing(ads112c04_t *ads112c04, uint8_t idac, idac_routing_t routing) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ads112c04_internal_lock_adc(ads112c04), TAG, "Failed to lock adc");
    esp_err_t ret = ads112c04_internal_set_idac_routing(ads112c04, idac, routing);
    ESP_RETURN_ON_ERROR(ads112c04_internal_unlock_adc(ads112c04), TAG, "Failed to unlock adc");
    return ret;
}

esp_err_t ads112c04_get_single_voltage_reading(
    ads112c04_t *ads112c04,
    float *voltage,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ads112c04_internal_lock_adc(ads112c04), TAG, "Failed to lock adc");
    esp_err_t ret = ads112c04_internal_get_single_voltage_reading(ads112c04, voltage, p_pin, n_pin, gain, pga_enabled);
    ESP_RETURN_ON_ERROR(ads112c04_internal_unlock_adc(ads112c04), TAG, "Failed to unlock adc");
    return ret;
}

esp_err_t ads112c04_get_single_temperature_reading(ads112c04_t *ads112c04, float *temperature) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ads112c04_internal_lock_adc(ads112c04), TAG, "Failed to lock adc");
    esp_err_t ret = ads112c04_internal_get_single_temperature_reading(ads112c04, temperature);
    ESP_RETURN_ON_ERROR(ads112c04_internal_unlock_adc(ads112c04), TAG, "Failed to unlock adc");
    return ret;
}