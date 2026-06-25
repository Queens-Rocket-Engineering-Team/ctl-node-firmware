#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "resistance_sensor.h"
#include "sensor.h"

static const char *TAG = "RESISTANCE_SENSOR";

static idac_current_t s_current_to_adc_enum(uint16_t current_val) {
    switch (current_val) {
    case 0:
        return IDAC_OFF;
    case 10:
        return IDAC_10_UA;
    case 50:
        return IDAC_50_UA;
    case 100:
        return IDAC_100_UA;
    case 250:
        return IDAC_250_UA;
    case 500:
        return IDAC_500_UA;
    case 1000:
        return IDAC_1000_UA;
    case 1500:
        return IDAC_1500_UA;
    default:
        return IDAC_INVALID_CURRENT;
    }
}

static idac_routing_t s_pin_to_adc_enum(ads112c04_pin_t pin) {
    switch (pin) {
    case ADS112C04_AIN0:
        return IDAC_AIN0;
    case ADS112C04_AIN1:
        return IDAC_AIN1;
    case ADS112C04_AIN2:
        return IDAC_AIN2;
    case ADS112C04_AIN3:
        return IDAC_AIN3;
    default:
        return IDAC_INVALID_ROUTING;
    }
}

esp_err_t resistance_sensor_init(
    resistance_sensor_t *resistance_sensor,
    const resistance_sensor_config_t *resistance_sensor_cfg
) {
    if (resistance_sensor == NULL || resistance_sensor_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_config_t sensor_cfg = {
        .adc = resistance_sensor_cfg->adc,
        .p_pin = resistance_sensor_cfg->pin,
        .n_pin = ADS112C04_AVSS,
        .gain = 4,
        .pga_enabled = false,
    };

    ESP_RETURN_ON_ERROR(
        sensor_init(&resistance_sensor->sensor, &sensor_cfg), TAG, "Failed to initialize resistance sensor"
    );

    resistance_sensor->injected_current_uA = resistance_sensor_cfg->injected_current_uA;
    resistance_sensor->r_short = resistance_sensor_cfg->r_short;
    resistance_sensor->unit = resistance_sensor_cfg->unit;
    return ESP_OK;
}

esp_err_t get_resistance_reading(resistance_sensor_t *resistance_sensor, float *resistance) {
    if (resistance_sensor == NULL || resistance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const idac_current_t current = s_current_to_adc_enum(resistance_sensor->injected_current_uA);
    const idac_routing_t routing = s_pin_to_adc_enum(resistance_sensor->sensor.p_pin);
    ESP_RETURN_ON_ERROR(ads112c04_set_idac_current(resistance_sensor->sensor.adc, current), TAG, "Failed to set IDAC current");
    ESP_RETURN_ON_ERROR(ads112c04_set_idac_routing(resistance_sensor->sensor.adc, 1, routing), TAG, "Failed to route IDAC");

    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_voltage_reading(&resistance_sensor->sensor, &voltage), TAG, "Failed to get resistance voltage reading"
    );
    
    ESP_RETURN_ON_ERROR(ads112c04_set_idac_routing(resistance_sensor->sensor.adc, 1, IDAC_DISABLED), TAG, "Failed to disable IDAC");

    const float resistance_ohms = (1e6 * voltage / resistance_sensor->injected_current_uA) - resistance_sensor->r_short;
    if (resistance_sensor->unit == RESISTANCE_SENSOR_OHMS) {
        *resistance = resistance_ohms;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
