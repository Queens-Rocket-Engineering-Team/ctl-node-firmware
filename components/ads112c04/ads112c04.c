#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "ads112c04.h"
#include "ads112c04_defines.h"

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

// send reset, start/sync, power down commands
static esp_err_t s_send_command(ads112c04_t *ads112c04, uint8_t cmd) {

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit(ads112c04->dev_handle, &cmd, 1, ADS112C04_I2C_TIMEOUT), TAG, "Failed to send command"
    );

    return ESP_OK;
}

// retrieve conversion from adc
static esp_err_t s_read_data(ads112c04_t *ads112c04, int16_t *data) {
    const uint8_t rdata_cmd = RDATA;
    uint8_t data_bytes[2] = {0};

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(ads112c04->dev_handle, &rdata_cmd, 1, data_bytes, 2, ADS112C04_I2C_TIMEOUT),
        TAG,
        "RDATA command failed"
    );

    *data = ((int16_t)data_bytes[0] << 8) | ((int16_t)data_bytes[1]);

    return ESP_OK;
}

static esp_err_t s_write_register(ads112c04_t *ads112c04, uint8_t reg, uint8_t write_byte) {
    // The WREG command is structured like: 0100 rrxx dddd dddd
    // Where rr is the register address and dddd dddd is the data to write.
    // This is sent in two parts: the command - 0100 rrxx, and
    // the data - dddddddd

    if (reg > 3) {
        ESP_LOGE(TAG, "Invalid register");
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t wreg[] = {WREG(reg), write_byte};

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit(ads112c04->dev_handle, wreg, 2, ADS112C04_I2C_TIMEOUT), TAG, "Failed to send WREG"
    );

    return ESP_OK;
}

static esp_err_t s_read_register(ads112c04_t *ads112c04, uint8_t reg, uint8_t *reg_byte) {
    // The RREG command is structured like: 0010 rrxx
    // The full rreg sequence takes two i2c transactions:
    // 1. Send the rreg command to the device.
    // 2. Read the response data from the device.

    if (reg > 3) {
        ESP_LOGE(TAG, "Invalid register");
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t rreg_cmd = RREG(reg);

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(ads112c04->dev_handle, &rreg_cmd, 1, reg_byte, 1, ADS112C04_I2C_TIMEOUT),
        TAG,
        "Failed to send RREG"
    );

    return ESP_OK;
}

static esp_err_t s_enable_internal_temperature(ads112c04_t *ads112c04) {
    uint8_t reg1;
    ESP_RETURN_ON_ERROR(s_read_register(ads112c04, 1, &reg1), TAG, "Failed to read register");
    reg1 |= TS_MASK;
    ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 1, reg1), TAG, "Failed to write to register");
    return ESP_OK;
}

static esp_err_t s_disable_internal_temperature(ads112c04_t *ads112c04) {
    uint8_t reg1;
    ESP_RETURN_ON_ERROR(s_read_register(ads112c04, 1, &reg1), TAG, "Failed to read register");
    reg1 &= ~TS_MASK;
    ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 1, reg1), TAG, "Failed to write to register");
    return ESP_OK;
}

// get the register gain bits for a given gain
static uint8_t s_get_gain_bits(uint8_t gain) {
    switch (gain) {
    case 1:
        return 0x00;
    case 2:
        return 0x01;
    case 4:
        return 0x02;
    case 8:
        return 0x03;
    case 16:
        return 0x04;
    case 32:
        return 0x05;
    case 64:
        return 0x06;
    case 128:
        return 0x07;
    default:
        return INVALID_GAIN;
    }
}

// get the register mux code for a given pin pair
static uint8_t s_get_mux_code(ads112c04_pin_t p_pin, ads112c04_pin_t n_pin) {
    if (p_pin > 4 || n_pin > 4) {
        return INVALID_MUX;
    }
    static const uint8_t lut[5][5] = {
        {INVALID_MUX, 0x00,        0x01,        0x02,        0x08       }, // p_pin == PIN0
        {0x03,        INVALID_MUX, 0x04,        0x05,        0x09       }, // p_pin == PIN1
        {INVALID_MUX, INVALID_MUX, INVALID_MUX, 0x06,        0x0A       }, // p_pin == PIN2
        {INVALID_MUX, INVALID_MUX, 0x07,        INVALID_MUX, 0x0B       }, // p_pin == PIN3
        {INVALID_MUX, INVALID_MUX, INVALID_MUX, INVALID_MUX, INVALID_MUX}  // p_pin == AVSS
    };
    return lut[p_pin][n_pin];
}

static esp_err_t s_set_inputs(
    ads112c04_t *ads112c04,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (n_pin == ADS112C04_AVSS && pga_enabled) {
        ESP_LOGE(TAG, "Cannot enable PGA for single-ended readings");
        return ESP_ERR_INVALID_ARG;
    }

    // check if given gain is valid for configuration
    uint8_t gain_bits = s_get_gain_bits(gain);
    if (gain_bits == INVALID_GAIN) {
        ESP_LOGE(TAG, "%d gain is not a valid gain", gain);
        return ESP_ERR_INVALID_ARG;
    }
    if (!pga_enabled) {
        if (gain_bits > 2) {
            ESP_LOGE(TAG, "%d gain is not a valid gain with PGA disabled", gain);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // get the mux code bits from pins
    uint8_t mux_code = s_get_mux_code(p_pin, n_pin);
    if (mux_code == INVALID_MUX) {
        ESP_LOGE(TAG, "Given pin pair is not a valid mux config");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t pga_bypass_bit = !pga_enabled;

    uint8_t reg0 = 0;
    reg0 |= (mux_code << 4) & MUX_MASK;
    reg0 |= (gain_bits << 1) & GAIN_MASK;
    reg0 |= pga_bypass_bit & PGA_BYPASS_MASK;

    ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 0, reg0), TAG, "Failed to write reg0");

    ads112c04->gain = gain;
    ads112c04->pga_enabled = pga_enabled;

    return ESP_OK;
}

static esp_err_t s_set_single_shot(ads112c04_t *ads112c04) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ads112c04->conversion_mode != CM_SINGLE_SHOT) {
        uint8_t reg1;
        ESP_RETURN_ON_ERROR(s_read_register(ads112c04, 1, &reg1), TAG, "Failed to read register");
        reg1 &= ~CM_MASK;
        ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 1, reg1), TAG, "Failed to write to register");
        ads112c04->conversion_mode = CM_SINGLE_SHOT;
    }

    return ESP_OK;
}

static esp_err_t s_set_idac_current(ads112c04_t *ads112c04, idac_current_t current) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (current > IDAC_1500_UA) {
        return ESP_ERR_INVALID_ARG;
    }
    if (current == ads112c04->idac_current) {
        return ESP_OK;
    }

    uint8_t reg2;
    ESP_RETURN_ON_ERROR(s_read_register(ads112c04, 2, &reg2), TAG, "Failed to read register");
    reg2 &= ~IDAC_MASK;
    reg2 |= current;
    ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 2, reg2), TAG, "Failed to write to register");
    
    ads112c04->idac_current = current;

    return ESP_OK;
}

static esp_err_t s_set_idac_routing(ads112c04_t *ads112c04, uint8_t idac, idac_routing_t routing) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (routing > IDAC_REFN) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg3;
    ESP_RETURN_ON_ERROR(s_read_register(ads112c04, 3, &reg3), TAG, "Failed to read register");

    if (idac == 1) {
        reg3 &= ~I1MUX_MASK;
        reg3 |= routing << 5;
    } else if (idac == 2) {
        reg3 &= ~I2MUX_MASK;
        reg3 |= routing << 2;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 3, reg3), TAG, "Failed to write to register");

    return ESP_OK;
}

// convert rdata bits to voltage
static float s_bits_to_voltage(ads112c04_t *ads112c04, int16_t raw_data) {
    float voltage = (raw_data * ads112c04->ref_voltage) / (32768.0f * ads112c04->gain); // 16 bit ADC
    return voltage;
}

// convert rdata bits to temperature
static float s_bits_to_temperature(int16_t raw_data) {
    raw_data = raw_data >> 2;             // 14 bit left-aligned reading
    float temperature = raw_data / 32.0f; // Divide for temp resolution
    return temperature;
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

    s_send_command(ads112c04, RESET);
    vTaskDelay(pdTICKS_TO_MS(5));

    // set default configuration values
    uint8_t reg1 = 0;
    reg1 |= (0x06 << 5) & DR_MASK; // set to 1000SPS
    reg1 |= (0x01 << 4) & MODE_MASK;
    reg1 |= (0x00 << 3) & CM_MASK;   // set to single shot mode
    reg1 |= (0x02 << 1) & VREF_MASK; // set ref voltage to AVDD-AVSS
    reg1 |= 0x00 & TS_MASK;          // disable temperature sensor mode
    ESP_RETURN_ON_ERROR(s_write_register(ads112c04, 1, reg1), TAG, "Failed to write reg1");

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
    if (s_get_gain_bits(gain) == INVALID_GAIN) {
        return false;
    }
    return true;
}

bool ads112c04_is_mux_valid(ads112c04_pin_t p_pin, ads112c04_pin_t n_pin) {
    if (s_get_mux_code(p_pin, n_pin) == INVALID_MUX) {
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

    if (xSemaphoreTake(ads112c04->mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = s_set_inputs(ads112c04, p_pin, n_pin, gain, pga_enabled);

    xSemaphoreGive(ads112c04->mutex);
    return ret;
}

esp_err_t ads112c04_set_single_shot(ads112c04_t *ads112c04) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ads112c04->mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = s_set_single_shot(ads112c04);

    xSemaphoreGive(ads112c04->mutex);
    return ret;
}

esp_err_t ads112c04_set_idac_current(ads112c04_t *ads112c04, idac_current_t current) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ads112c04->mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = s_set_idac_current(ads112c04, current);

    xSemaphoreGive(ads112c04->mutex);
    return ret;
}

esp_err_t ads112c04_set_idac_routing(ads112c04_t *ads112c04, uint8_t idac, idac_routing_t routing) {
    if (ads112c04 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ads112c04->mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = s_set_idac_routing(ads112c04, idac, routing);

    xSemaphoreGive(ads112c04->mutex);
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
    if (ads112c04 == NULL || voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (p_pin > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    if (n_pin > 4) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ads112c04->mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(ads112c04->semaphore_DRDY, 0); // clear semaphore without waiting
    ESP_RETURN_ON_ERROR(s_set_single_shot(ads112c04), TAG, "Set single shot failed");
    ESP_RETURN_ON_ERROR(s_set_inputs(ads112c04, p_pin, n_pin, gain, pga_enabled), TAG, "Set inputs failed");

    ESP_RETURN_ON_ERROR(s_send_command(ads112c04, START_SYNC), TAG, "START/SYNC command failed");

    // wait until DRDY pin pulls low
    if (xSemaphoreTake(ads112c04->semaphore_DRDY, pdTICKS_TO_MS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    int16_t raw_data = 0;
    ESP_RETURN_ON_ERROR(s_read_data(ads112c04, &raw_data), TAG, "Failed to read conversion data");

    *voltage = s_bits_to_voltage(ads112c04, raw_data);

    xSemaphoreGive(ads112c04->mutex);

    return ESP_OK;
}

esp_err_t ads112c04_get_single_temperature_reading(ads112c04_t *ads112c04, float *temperature) {
    if (ads112c04 == NULL || temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ads112c04->mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(ads112c04->semaphore_DRDY, 0); // clear semaphore without waiting
    ESP_RETURN_ON_ERROR(s_set_single_shot(ads112c04), TAG, "Set single shot failed");
    ESP_RETURN_ON_ERROR(s_enable_internal_temperature(ads112c04), TAG, "Failed to enable internal temperature sensor");

    ESP_RETURN_ON_ERROR(s_send_command(ads112c04, START_SYNC), TAG, "START/SYNC command failed");

    // wait until DRDY pin pulls low
    if (xSemaphoreTake(ads112c04->semaphore_DRDY, pdTICKS_TO_MS(100)) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    int16_t raw_data = 0;
    ESP_RETURN_ON_ERROR(s_read_data(ads112c04, &raw_data), TAG, "Failed to read conversion data");
    ESP_RETURN_ON_ERROR(
        s_disable_internal_temperature(ads112c04), TAG, "Failed to disable internal temperature sensor"
    );

    *temperature = s_bits_to_temperature(raw_data);

    xSemaphoreGive(ads112c04->mutex);

    return ESP_OK;
}
