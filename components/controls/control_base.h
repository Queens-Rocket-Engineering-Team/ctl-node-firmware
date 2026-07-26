#pragma once

#include <esp_err.h>

typedef struct control_base control_base_t;

struct control_base {
    esp_err_t (*set_default)(control_base_t *control);
};