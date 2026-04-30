#include <esp_err.h>

#include "ads112c04.h"

esp_err_t ads112c04_internal_send_command(ads112c04_t *ads112c04, uint8_t cmd);

esp_err_t ads112c04_internal_read_data(ads112c04_t *ads112c04, int16_t *data);

esp_err_t ads112c04_internal_write_register(ads112c04_t *ads112c04, uint8_t reg, uint8_t write_byte);

esp_err_t ads112c04_internal_read_register(ads112c04_t *ads112c04, uint8_t reg, uint8_t *reg_byte);

esp_err_t ads112c04_internal_enable_internal_temperature(ads112c04_t *ads112c04);

esp_err_t ads112c04_internal_disable_internal_temperature(ads112c04_t *ads112c04);

uint8_t ads112c04_internal_get_gain_bits(uint8_t gain);

uint8_t ads112c04_internal_get_mux_code(ads112c04_pin_t p_pin, ads112c04_pin_t n_pin);

esp_err_t ads112c04_internal_set_inputs(
    ads112c04_t *ads112c04,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
);

esp_err_t ads112c04_internal_set_single_shot(ads112c04_t *ads112c04);

esp_err_t ads112c04_internal_set_idac_current(ads112c04_t *ads112c04, idac_current_t current);

esp_err_t ads112c04_internal_set_idac_routing(ads112c04_t *ads112c04, uint8_t idac, idac_routing_t routing);

esp_err_t ads112c04_internal_enable_internal_temperature(ads112c04_t *ads112c04);

esp_err_t ads112c04_internal_disable_internal_temperature(ads112c04_t *ads112c04);

esp_err_t ads112c04_internal_get_single_voltage_reading(
    ads112c04_t *ads112c04,
    float *voltage,
    ads112c04_pin_t p_pin,
    ads112c04_pin_t n_pin,
    uint8_t gain,
    bool pga_enabled
);

esp_err_t ads112c04_internal_get_single_temperature_reading(ads112c04_t *ads112c04, float *temperature);

esp_err_t ads112c04_internal_lock_adc(ads112c04_t *ads112c04);

esp_err_t ads112c04_internal_unlock_adc(ads112c04_t *ads112c04);