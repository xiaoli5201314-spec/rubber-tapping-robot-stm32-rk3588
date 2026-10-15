#include "chassis_math.h"

static int32_t ChassisMath_Abs32(int32_t value)
{
    return (value < 0) ? -value : value;
}

int16_t ChassisMath_PulseToCommand(uint32_t pulse_us, uint16_t center_us,
                                   uint16_t deadband_us, uint8_t invert)
{
    int32_t pulse;
    int32_t center = (int32_t)center_us;
    int32_t deadband = (int32_t)deadband_us;
    int32_t lower = center - deadband;
    int32_t upper = center + deadband;
    int32_t command;

    if (pulse_us < 1000U) {
        pulse = 1000;
    } else if (pulse_us > 2000U) {
        pulse = 2000;
    } else {
        pulse = (int32_t)pulse_us;
    }

    if ((pulse >= lower) && (pulse <= upper)) {
        return 0;
    }

    if (pulse < lower) {
        int32_t span = lower - 1000;
        command = (span > 0) ? ((pulse - lower) * 1000) / span : -1000;
    } else {
        int32_t span = 2000 - upper;
        command = (span > 0) ? ((pulse - upper) * 1000) / span : 1000;
    }

    if (command < -1000) {
        command = -1000;
    } else if (command > 1000) {
        command = 1000;
    }

    if (invert != 0U) {
        command = -command;
    }
    return (int16_t)command;
}

ChassisCommand ChassisMath_Mix(int16_t throttle, int16_t steering)
{
    int32_t left = (int32_t)throttle + (int32_t)steering;
    int32_t right = (int32_t)throttle - (int32_t)steering;
    int32_t max_magnitude = ChassisMath_Abs32(left);
    int32_t right_magnitude = ChassisMath_Abs32(right);
    ChassisCommand command;

    if (right_magnitude > max_magnitude) {
        max_magnitude = right_magnitude;
    }
    if (max_magnitude > 1000) {
        left = (left * 1000) / max_magnitude;
        right = (right * 1000) / max_magnitude;
    }

    command.left = (int16_t)left;
    command.right = (int16_t)right;
    return command;
}
