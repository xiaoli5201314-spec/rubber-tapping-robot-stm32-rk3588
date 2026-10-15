#include "motor_profile.h"

#include "board_config.h"

static MotorOutput MotorProfile_BrakeOutput(void)
{
    MotorOutput output = {MOTOR_DIR_BRAKE, 0U};

    return output;
}

static int16_t MotorProfile_ClampTarget(int16_t target_permille)
{
    if (target_permille > (int16_t)MOTOR_MAX_OUTPUT_PERMILLE) {
        return (int16_t)MOTOR_MAX_OUTPUT_PERMILLE;
    }

    if (target_permille < -(int16_t)MOTOR_MAX_OUTPUT_PERMILLE) {
        return -(int16_t)MOTOR_MAX_OUTPUT_PERMILLE;
    }

    return target_permille;
}

static MotorDirection MotorProfile_DirectionForTarget(int16_t target_permille)
{
    if (target_permille > 0) {
        return MOTOR_DIR_FORWARD;
    }

    if (target_permille < 0) {
        return MOTOR_DIR_REVERSE;
    }

    return MOTOR_DIR_BRAKE;
}

static uint16_t MotorProfile_MagnitudeForTarget(int16_t target_permille)
{
    if (target_permille < 0) {
        return (uint16_t)(-(int32_t)target_permille);
    }

    return (uint16_t)target_permille;
}

static uint16_t MotorProfile_RampToward(uint16_t current_permille,
                                         uint16_t target_permille)
{
    if (current_permille < target_permille) {
        if ((uint16_t)(target_permille - current_permille) >
            (uint16_t)MOTOR_RAMP_STEP_PER_10MS) {
            return (uint16_t)(current_permille + MOTOR_RAMP_STEP_PER_10MS);
        }

        return target_permille;
    }

    if (current_permille > target_permille) {
        if ((uint16_t)(current_permille - target_permille) >
            (uint16_t)MOTOR_RAMP_STEP_PER_10MS) {
            return (uint16_t)(current_permille - MOTOR_RAMP_STEP_PER_10MS);
        }

        return target_permille;
    }

    return current_permille;
}

MotorOutput MotorProfile_Update(MotorProfile *profile, int16_t target_permille,
                                uint32_t now_ms, uint8_t emergency_stop)
{
    MotorDirection target_direction;
    uint16_t target_magnitude;
    MotorOutput output;

    if (emergency_stop != 0U) {
        profile->active_direction = MOTOR_DIR_BRAKE;
        profile->pending_direction = MOTOR_DIR_BRAKE;
        profile->pwm_permille = 0U;
        profile->brake_start_ms = 0U;
        profile->reversal_pending = 0U;
        return MotorProfile_BrakeOutput();
    }

    target_permille = MotorProfile_ClampTarget(target_permille);
    target_direction = MotorProfile_DirectionForTarget(target_permille);
    target_magnitude = MotorProfile_MagnitudeForTarget(target_permille);

    if (profile->reversal_pending != 0U) {
        /* Neutral or another target cannot shorten an existing zero hold. */
        if ((uint32_t)(now_ms - profile->brake_start_ms) < MOTOR_REVERSE_BRAKE_MS) {
            return MotorProfile_BrakeOutput();
        }

        /* Restart in the current requested direction, never a stale target. */
        profile->active_direction = MOTOR_DIR_BRAKE;
        profile->pending_direction = MOTOR_DIR_BRAKE;
        profile->reversal_pending = 0U;
    }

    if (target_direction == MOTOR_DIR_BRAKE) {
        profile->pwm_permille = MotorProfile_RampToward(profile->pwm_permille, 0U);
        if ((profile->pwm_permille == 0U) &&
            (profile->active_direction != MOTOR_DIR_BRAKE)) {
            profile->active_direction = MOTOR_DIR_BRAKE;
            profile->pending_direction = MOTOR_DIR_BRAKE;
            profile->brake_start_ms = now_ms;
            profile->reversal_pending = 1U;
        }
    } else if (profile->active_direction == MOTOR_DIR_BRAKE) {
        profile->active_direction = target_direction;
        profile->pwm_permille = MotorProfile_RampToward(0U, target_magnitude);
    } else if (profile->active_direction == target_direction) {
        profile->pwm_permille = MotorProfile_RampToward(profile->pwm_permille,
                                                         target_magnitude);
    } else {
        profile->pwm_permille = MotorProfile_RampToward(profile->pwm_permille, 0U);
        if (profile->pwm_permille == 0U) {
            profile->pending_direction = target_direction;
            profile->brake_start_ms = now_ms;
            profile->reversal_pending = 1U;
        }
    }

    if (profile->pwm_permille == 0U) {
        return MotorProfile_BrakeOutput();
    }

    output.direction = profile->active_direction;
    output.pwm_permille = profile->pwm_permille;
    return output;
}
