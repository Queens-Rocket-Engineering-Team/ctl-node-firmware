#pragma once

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral;
    float setpoint;
    float prev_measurement;
    float dt;
    float output_max;
    float output_min;
} pid_ctx_t;

float pid_step(pid_ctx_t *pid, float measurement);