#pragma once

#include "sensor_base.h"
#include "current_sensor.h"
#include "load_cell.h"
#include "pressure_transducer.h"
#include "resistance_sensor.h"
#include "thermocouple.h"
#include "hall_effect_current.h"

typedef union {
    sensor_base_t base;
    thermocouple_t thermocouple;
    pressure_transducer_t pressure_transducer;
    load_cell_t load_cell;
    current_sensor_t current_sensor;
    he_current_t he_current;
    resistance_sensor_t resistance_sensor;
} sensor_t;
