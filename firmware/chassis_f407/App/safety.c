#include "safety.h"

#include <string.h>

#include "board_config.h"

#define SAFETY_VALID_MIN_US 800U
#define SAFETY_VALID_MAX_US 2200U
#define SAFETY_LOW_ACTIVE_ARM_THRESHOLD_US 1300U

static uint8_t Safety_IsPulseInRange(uint32_t pulse_us)
{
    return (uint8_t)((pulse_us >= SAFETY_VALID_MIN_US) &&
                     (pulse_us <= SAFETY_VALID_MAX_US));
}

static uint8_t Safety_AreInputsFreshAndInRange(const SafetyInputs *inputs)
{
    return (uint8_t)((inputs->ch1_fresh != 0U) &&
                     (inputs->ch2_fresh != 0U) &&
                     (inputs->ch5_fresh != 0U) &&
                     (Safety_IsPulseInRange(inputs->ch1_us) != 0U) &&
                     (Safety_IsPulseInRange(inputs->ch2_us) != 0U) &&
                     (Safety_IsPulseInRange(inputs->ch5_us) != 0U));
}

static uint8_t Safety_IsChannelNeutral(uint32_t pulse_us)
{
    return (uint8_t)((pulse_us >= (RC_CENTER_US - RC_DEADBAND_US)) &&
                     (pulse_us <= (RC_CENTER_US + RC_DEADBAND_US)));
}

static uint8_t Safety_AreDriveChannelsNeutral(const SafetyInputs *inputs)
{
    return (uint8_t)((Safety_IsChannelNeutral(inputs->ch1_us) != 0U) &&
                     (Safety_IsChannelNeutral(inputs->ch2_us) != 0U));
}

static uint8_t Safety_IsArmSwitchActive(uint32_t ch5_us)
{
    if (Safety_IsPulseInRange(ch5_us) == 0U) {
        return 0U;
    }

#if RC_ARM_ACTIVE_HIGH
    return (uint8_t)(ch5_us >= RC_ARM_THRESHOLD_US);
#else
    return (uint8_t)(ch5_us <= SAFETY_LOW_ACTIVE_ARM_THRESHOLD_US);
#endif
}

static uint8_t Safety_IsArmSwitchDisarmed(uint32_t ch5_us)
{
    if (Safety_IsPulseInRange(ch5_us) == 0U) {
        return 0U;
    }

#if RC_ARM_ACTIVE_HIGH
    return (uint8_t)(ch5_us < RC_ARM_THRESHOLD_US);
#else
    return (uint8_t)(ch5_us > SAFETY_LOW_ACTIVE_ARM_THRESHOLD_US);
#endif
}

void Safety_Init(SafetyStateMachine *machine)
{
    (void)memset(machine, 0, sizeof(*machine));
}

void Safety_Update(SafetyStateMachine *machine, const SafetyInputs *inputs, uint32_t now_ms)
{
    uint8_t inputs_acceptable = Safety_AreInputsFreshAndInRange(inputs);
    uint8_t drive_neutral = Safety_AreDriveChannelsNeutral(inputs);
    uint8_t arm_active = Safety_IsArmSwitchActive(inputs->ch5_us);
    uint8_t arm_disarmed = Safety_IsArmSwitchDisarmed(inputs->ch5_us);

    if (inputs_acceptable == 0U) {
        if (machine->state == SAFETY_ARMED) {
            machine->state = SAFETY_FAILSAFE;
        } else if (machine->state == SAFETY_WAIT_NEUTRAL) {
            machine->state = SAFETY_LOCKED;
            machine->neutral_start_ms = 0U;
        }
        return;
    }

    switch (machine->state) {
    case SAFETY_LOCKED:
        if ((arm_disarmed != 0U) && (drive_neutral != 0U)) {
            machine->state = SAFETY_WAIT_NEUTRAL;
            machine->neutral_start_ms = now_ms;
        }
        break;

    case SAFETY_WAIT_NEUTRAL:
        if (drive_neutral == 0U) {
            machine->state = SAFETY_LOCKED;
            machine->neutral_start_ms = 0U;
        } else if ((arm_active == 0U) && (arm_disarmed == 0U)) {
            machine->state = SAFETY_LOCKED;
            machine->neutral_start_ms = 0U;
        } else if (arm_active != 0U) {
            if ((uint32_t)(now_ms - machine->neutral_start_ms) >=
                RC_ARM_NEUTRAL_HOLD_MS) {
                machine->state = SAFETY_ARMED;
            } else {
                /* The entire neutral hold must occur while disarmed.
                   An early high switch requires a fresh low-switch hold. */
                machine->state = SAFETY_LOCKED;
                machine->neutral_start_ms = 0U;
            }
        }
        break;

    case SAFETY_ARMED:
        if (arm_active == 0U) {
            machine->state = SAFETY_LOCKED;
            machine->neutral_start_ms = 0U;
        }
        break;

    case SAFETY_FAILSAFE:
        if (arm_disarmed != 0U) {
            machine->state = SAFETY_LOCKED;
            machine->neutral_start_ms = 0U;
        }
        break;

    default:
        machine->state = SAFETY_LOCKED;
        machine->neutral_start_ms = 0U;
        break;
    }
}

uint8_t Safety_IsDriveAllowed(const SafetyStateMachine *machine)
{
    return (uint8_t)(machine->state == SAFETY_ARMED);
}
