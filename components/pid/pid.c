#include <stdbool.h>

#include "pid.h"

float pid_step(pid_ctx_t *pid, float measurement) {
    const float error = pid->setpoint - measurement;

    const float proportional = pid->kp * error;
    const float derivative = pid->kd * (pid->prev_measurement - measurement) / pid->dt;

    // compute output before updating integral
    float unsaturated_output = proportional + pid->integral + derivative;

    // only integrate if not saturated or if integrating moves out of saturation
    bool max_saturated = (unsaturated_output >= pid->output_max) && (error > 0);
    bool min_saturated = (unsaturated_output <= pid->output_min) && (error < 0);

    if (!max_saturated && !min_saturated) {
        pid->integral += pid->ki * error * pid->dt;
        
        if (pid->integral > pid->output_max) {
            pid->integral = pid->output_max;
        } else if (pid->integral < pid->output_min) {
            pid->integral = pid->output_min;
        }
    }

    float output = proportional + pid->integral + derivative;
    if (output > pid->output_max) {
        output = pid->output_max;
    } else if (output < pid->output_min) {
        output = pid->output_min;
    }

    pid->prev_measurement = measurement;
    return output;
}