import argparse
import json

def read_json(file_path: str) -> tuple[dict, str]:
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            json_dict = json.load(file)
            json_str = json.dumps(json_dict, separators=(',',':'))
            return json_dict, json_str
    except Exception as e:
        print(f"Failed to read config file: {e}")   
        raise
    

parser = argparse.ArgumentParser(description="Convert JSON config to c constants and initialization code")
parser.add_argument('config', type=str)
parser.add_argument('mapping', type=str)
parser.add_argument('header', type=str)
parser.add_argument('source', type=str)
args = parser.parse_args()

config, config_str = read_json(args.config)
mapping, _ = read_json(args.mapping)

# .h file generation

num_adcs = len(mapping['ADC_map'])
num_sensors = sum(len(sensor_group) for sensor_group in config['sensor_info'].values())
num_controls = len(config['controls'])

sda_pin = mapping['i2c_bus']['sda_pin']
scl_pin = mapping['i2c_bus']['scl_pin']
i2c_freq = mapping['i2c_bus']['frequency_Hz']
wifi_indicator_pin = mapping.get('wifi_indicator_pin')

wifi_pin_define = ''
if wifi_indicator_pin is not None:
    wifi_pin_define = f'#define CONFIG_WIFI_INDICATOR_PIN {wifi_indicator_pin}'

header_content = f"""\
#pragma once

#include <esp_err.h>
#include <stdint.h>

#include "ads112c04.h"
#include "sensor.h"
#include "control.h"

// Auto-generated header from esp_config.json and esp_mapping.json

extern const char json_config_str[];
#define JSON_CONFIG_LEN {len(config_str)}

#define CONFIG_NUM_ADCS {num_adcs}
#define CONFIG_NUM_SENSORS {num_sensors}
#define CONFIG_NUM_CONTROLS {num_controls}

#define CONFIG_SDA_PIN {sda_pin}
#define CONFIG_SCL_PIN {scl_pin}
#define CONFIG_I2C_FREQUENCY {i2c_freq}

{wifi_pin_define}

esp_err_t config_ads112c04s_init(ads112c04_t adcs[], size_t len_adcs, i2c_master_bus_handle_t bus_handle);

esp_err_t config_sensors_init(sensor_t sensors[], size_t sensors_len, ads112c04_t adcs[], size_t len_adcs);

esp_err_t config_controls_init(control_t controls[], size_t len_controls);
"""

try:
    with open(args.header, 'w') as header:
        header.write(header_content)
        
except Exception as e:
    print(f"Failed to create header file: {e}")


# .c file generation

# template for mapping between values in the json config and corresponding c sensor config structs
sensor_templates = {
    'thermocouple': {
        'cfg_struct_name': 'thermocouple_config_t',
        'init_func': 'thermocouple_init',
        'cfg_struct_fields': {
            'p_pin': 'p_pin',
            'n_pin': 'n_pin',
            'unit': 'unit',
        },
        'unit': {
            'c': 'SENSOR_UNIT_C',
            'k': 'SENSOR_UNIT_K',
            'f': 'SENSOR_UNIT_F',
        },
    },
    'pressure_transducer': {
        'cfg_struct_name': 'pressure_transducer_config_t',
        'init_func': 'pressure_transducer_init',
        'cfg_struct_fields': {
            'pin': 'pin',
            'resistor_ohms': 'resistor_ohms',
            'max_pressure_psi': 'max_pressure_PSI',
            'unit': 'unit',
        },
        'unit': {
            'psi': 'SENSOR_UNIT_PSI',
            'bar': 'SENSOR_UNIT_BAR',
            'pa': 'SENSOR_UNIT_PA',
        },
    },
    'load_cell': {
        'cfg_struct_name': 'load_cell_config_t',
        'init_func': 'load_cell_init',
        'cfg_struct_fields': {
            'p_pin': 'p_pin',
            'n_pin': 'n_pin',
            'load_rating_N': 'load_rating_N',
            'excitation_V': 'excitation_V',
            'sensitivity_vV': 'sensitivity_vV',
            'unit': 'unit',
        },
        'unit': {
            'kg': 'SENSOR_UNIT_KG',
            'n': 'SENSOR_UNIT_N',
        },
    },
    'resistance_sensor': {
        'cfg_struct_name': 'resistance_sensor_config_t',
        'init_func': 'resistance_sensor_init',
        'cfg_struct_fields': {
            'pin': 'pin',
            'injected_current_uA': 'injected_current_uA',
            'r_short': 'r_short',
            'unit': 'unit',
        },
        'unit': {
            'ohms': 'SENSOR_UNIT_OHMS',
        },
    },
    'current_sensor': {
        'cfg_struct_name': 'current_sensor_config_t',
        'init_func': 'current_sensor_init',
        'cfg_struct_fields': {
            'pin': 'pin',
            'shunt_resistor_ohms': 'shunt_resistor_ohms',
            'csa_gain': 'csa_gain',
            'unit': 'unit',
        },
        'unit': {
            'a': 'SENSOR_UNIT_A',
        },
    },
}
# json name to c enum for controls
control_fields = {
    'default_state': {
        'open': 'CONTROL_OPEN',
        'closed': 'CONTROL_CLOSED',
    },
    'contact': {
        'solenoid': 'CONTROL_NC',
        'relay': 'CONTROL_NO',
    },
}

def generate_adc_init(adc_cfg: dict, index: int) -> str:

    adc_addr = adc_cfg['addr']
    adc_DRDY = adc_cfg['DRDY_pin']

    return f"""\
    {{
    const ads112c04_config_t adc_cfg = {{
        .addr = {adc_addr},
        .drdy_pin = {adc_DRDY},
        .bus_handle = bus_handle,
        .bus_frequency = CONFIG_I2C_FREQUENCY,
    }};

    ESP_RETURN_ON_ERROR(ads112c04_init(&adcs[{index}], &adc_cfg), TAG, "Failed to initialize ADS112C04, index {index}");
    }}
    """

def generate_sensor_init(sensor_cfg: dict, sensor_type: str, mapping: dict, index: int) -> str:
    template = sensor_templates[sensor_type]

    sensor_key = sensor_cfg['sensor_index']
    pin_map = mapping['sensor_map'][sensor_key]

    adc_key = pin_map['ADC_index']
    adc_addr = mapping['ADC_map'][adc_key]['addr']

    # get the required config fields from the sensor template
    cfg_struct_fields = []
    for struct_field, json_key in template['cfg_struct_fields'].items():

        val = sensor_cfg.get(json_key)
        if val is None:
            val = pin_map.get(json_key)

        if struct_field == 'unit':
            val = template['unit'][val.casefold()]

        cfg_struct_fields.append(f'        .{struct_field} = {val},')

    cfg_struct_fields_str = '\n'.join(cfg_struct_fields)

    return f"""\
    {{
    ads112c04_t *adc = s_find_adc_from_addr(adcs, len_adcs, {adc_addr});
    if (adc == NULL) {{
        return ESP_ERR_NOT_FOUND;
    }}

    const {template['cfg_struct_name']} cfg = {{
        .adc = adc,
{cfg_struct_fields_str}
    }};

    ESP_RETURN_ON_ERROR({template['init_func']}(&sensors[{index}].{sensor_type}, &cfg), TAG, "Failed to initialize {sensor_type}, index {index}");
    }}
    """

def generate_control_init(control_cfg: dict, index: int) -> str:

    control_key = control_cfg['control_index']
    pin_map = mapping['control_map'][control_key]

    pin = pin_map['pin']

    json_default_state = control_cfg['default_state'].casefold()
    default_state = control_fields['default_state'][json_default_state]

    control_type = control_cfg['type'].casefold()
    contact = control_fields['contact'][control_type]

    return f"""\
    {{
    const control_config_t control_cfg = {{
        .gpio_num = {pin},
        .default_state = {default_state},
        .contact = {contact},
    }};

    ESP_RETURN_ON_ERROR(control_init(&controls[{index}], &control_cfg), TAG, "Failed to initialize control, index {index}");
    }}
    """

adcs_init_code = []
adcs_initialized = 0
for adc_cfg in mapping['ADC_map'].values():
    adcs_init_code.append(generate_adc_init(adc_cfg, adcs_initialized))
    adcs_initialized += 1

adcs_init_code = '\n'.join(adcs_init_code)

sensors_init_code = []
sensors_initialized = 0
for sensor_type, sensors in config['sensor_info'].items():
    for sensor_cfg in sensors.values():
        sensors_init_code.append(generate_sensor_init(sensor_cfg, sensor_type, mapping, sensors_initialized))
        sensors_initialized += 1

sensors_init_code = '\n'.join(sensors_init_code)

controls_init_code = []
controls_initialized = 0
for control_cfg in config['controls'].values():
    controls_init_code.append(generate_control_init(control_cfg, controls_initialized))
    controls_initialized += 1

controls_init_code = '\n'.join(controls_init_code)

source_content = f"""\
#include <esp_check.h>
#include <esp_err.h>
#include <stdint.h>

#include "ads112c04.h"
#include "sensor.h"
#include "control.h"

#include "config_json.h"

static const char *TAG = "CONFIG JSON";

// Auto-generated code from esp_config.json and esp_mapping.json

const char json_config_str[] = "{config_str.replace(r'"', r'\"')}";

static ads112c04_t *s_find_adc_from_addr(ads112c04_t adcs[], size_t len_adcs, uint8_t addr) {{
    for (size_t i = 0; i < len_adcs; i++) {{
        if (ads112c04_get_address(&adcs[i]) == addr) {{
                    return &adcs[i];
        }}
    }}
    return NULL;
}}

esp_err_t config_ads112c04s_init(ads112c04_t adcs[], size_t len_adcs, i2c_master_bus_handle_t bus_handle) {{
    if (adcs == NULL || bus_handle == NULL) {{
        return ESP_ERR_INVALID_ARG;
    }}
    if (len_adcs < CONFIG_NUM_ADCS) {{
        return ESP_ERR_NO_MEM;
    }}

{adcs_init_code}
    return ESP_OK;
}}

esp_err_t config_sensors_init(sensor_t sensors[], size_t sensors_len, ads112c04_t adcs[], size_t len_adcs) {{
    if (sensors == NULL || adcs == NULL) {{
        return ESP_ERR_INVALID_ARG;
    }}
    if (sensors_len < CONFIG_NUM_SENSORS) {{
        return ESP_ERR_NO_MEM;
    }}

{sensors_init_code}
    return ESP_OK;
}}

esp_err_t config_controls_init(control_t controls[], size_t len_controls) {{
    if (controls == NULL) {{
        return ESP_ERR_INVALID_ARG;
    }}
    if (len_controls < CONFIG_NUM_CONTROLS) {{
        return ESP_ERR_NO_MEM;
    }}

{controls_init_code}
    return ESP_OK;
}}
"""

try:
    with open(args.source, 'w') as source:
        source.write(source_content)
        
except Exception as e:
    print(f"Failed to create source file: {e}")





