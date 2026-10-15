#ifndef MOTOR_PROFILE_H
#define MOTOR_PROFILE_H

#include <stdint.h>

typedef enum {
    MOTOR_DIR_BRAKE = 0,
    MOTOR_DIR_FORWARD,
    MOTOR_DIR_REVERSE
} MotorDirection;

typedef struct {
    MotorDirection direction;
    uint16_t pwm_permille;
} MotorOutput;

typedef struct {
    MotorDirection active_direction;
    MotorDirection pending_direction;
    uint16_t pwm_permille;
    uint32_t brake_start_ms;
    uint8_t reversal_pending;
} MotorProfile;

MotorOutput MotorProfile_Update(MotorProfile *profile, int16_t target_permille,
                                uint32_t now_ms, uint8_t emergency_stop);

#endif
