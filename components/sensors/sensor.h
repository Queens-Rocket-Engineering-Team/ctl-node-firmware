#pragma once

#include "current_sensor.h"
#include "load_cell.h"
#include "pressure_transducer.h"
#include "resistance_sensor.h"
#include "thermocouple.h"

typedef union {
    thermocouple_t thermocouple;
    pressure_transducer_t pressure_transducer;
    load_cell_t load_cell;
    current_sensor_t current_sensor;
    resistance_sensor_t resistance_sensor;
} sensor_t;
