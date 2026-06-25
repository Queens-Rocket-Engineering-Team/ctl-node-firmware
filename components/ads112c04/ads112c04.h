#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <stdint.h>

typedef enum : uint8_t {
    ADS112C04_AIN0 = 0,
    ADS112C04_AIN1 = 1,
    ADS112C04_AIN2 = 2,
    ADS112C04_AIN3 = 3,
    ADS112C04_AVSS = 4,
} ads112c04_pin_t;

typedef enum : uint8_t {
    IDAC_OFF = 0x00,
    IDAC_10_UA = 0x01,
    IDAC_50_UA = 0x02,
    IDAC_100_UA = 0x03,
    IDAC_250_UA = 0x04,
    IDAC_500_UA = 0x05,
    IDAC_1000_UA = 0x06,
    IDAC_1500_UA = 0x07,
    IDAC_INVALID_CURRENT = 0xFF,
} idac_current_t;

typedef enum : uint8_t {
    IDAC_DISABLED = 0x00,
    IDAC_AIN0 = 0x01,
    IDAC_AIN1 = 0x02,
    IDAC_AIN2 = 0x03,
    IDAC_AIN3 = 0x04,
    IDAC_REFP = 0x05,
    IDAC_REFN = 0x06,
    IDAC_INVALID_ROUTING = 0xFF,
} idac_routing_t;

typedef enum : uint8_t {
    CM_SINGLE_SHOT,
    CM_CONTINUOUS
} conversion_mode_t;

typedef struct {
    StaticSemaphore_t mutex_buffer;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t semaphore_buffer_DRDY;
    SemaphoreHandle_t semaphore_DRDY;
    i2c_master_dev_handle_t dev_handle;
    float ref_voltage;
    conversion_mode_t conversion_mode;
    idac_current_t idac_current;
    uint8_t addr;
    uint8_t gain;
    bool pga_enabled;
} ads112c04_t;

typedef struct {
    uint8_t addr;
    uint8_t drdy_pin;
    i2c_master_bus_handle_t bus_handle;
    uint32_t bus_frequency;
} ads112c04_config_t;

esp_err_t ads112c04_init(ads112c04_t *ads112c04, const ads112c04_config_t *ads112c04_cfg);

uint8_t ads112c04_get_address(const ads112c04_t *ads112c04);
bool ads112c04_is_gain_valid(uint8_t gain);
bool ads112c04_is_mux_valid(ads112c04_pin_t p_pin, ads112c04_pin_t n_pin);

esp_err_t ads112c04_set_inputs(
    ads112c04_t *ads112c04,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
);

esp_err_t ads112c04_set_single_shot(ads112c04_t *ads112c04);

esp_err_t ads112c04_set_idac_current(ads112c04_t *ads112c04, idac_current_t current);
esp_err_t ads112c04_set_idac_routing(ads112c04_t *ads112c04, uint8_t idac, idac_routing_t routing);

esp_err_t ads112c04_get_single_voltage_reading(
    ads112c04_t *ads112c04,
    float *voltage,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
);

esp_err_t ads112c04_get_single_temperature_reading(ads112c04_t *ads112c04, float *temperature);
