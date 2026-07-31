#pragma once

#include <esp_err.h>
#include <stddef.h>
#include <ads112c04.h>

typedef struct {
    ads112c04_t *adcs;
    size_t num_adcs;
} board_ctx_t;

esp_err_t board_setup(board_ctx_t *board_ctx);