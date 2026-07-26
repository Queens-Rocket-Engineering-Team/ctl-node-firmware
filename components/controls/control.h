#pragma once

#include "h_bool_control.h"
#include "virtual_uint32_control.h"

typedef enum {
    CONTROL_TYPE_H_BOOL,
    CONTROL_TYPE_V_UINT32,
} control_type_t;

typedef struct {
    control_type_t control_type;
    union {
        control_base_t base;
        h_bool_control_t h_bool;
        v_uint32_control_t v_uint32;
    } control;
} control_t;