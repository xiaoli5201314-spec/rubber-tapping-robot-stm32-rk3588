#ifndef CHASSIS_MATH_H
#define CHASSIS_MATH_H

#include <stdint.h>

typedef struct {
    int16_t left;
    int16_t right;
} ChassisCommand;

int16_t ChassisMath_PulseToCommand(uint32_t pulse_us, uint16_t center_us,
                                   uint16_t deadband_us, uint8_t invert);
ChassisCommand ChassisMath_Mix(int16_t throttle, int16_t steering);

#endif
