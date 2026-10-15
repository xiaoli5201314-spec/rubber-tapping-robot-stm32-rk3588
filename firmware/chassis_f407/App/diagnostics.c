#include "diagnostics.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "usart.h"

#define DIAGNOSTICS_BUFFER_SIZE 256U

static char diagnostics_buffer[DIAGNOSTICS_BUFFER_SIZE];

static const char *Diagnostics_StateName(SafetyState state)
{
    switch (state) {
    case SAFETY_LOCKED:
        return "LOCKED";
    case SAFETY_WAIT_NEUTRAL:
        return "WAIT";
    case SAFETY_ARMED:
        return "ARMED";
    case SAFETY_FAILSAFE:
        return "FAILSAFE";
    default:
        return "UNKNOWN";
    }
}

static const char *Diagnostics_DirectionName(MotorDirection direction)
{
    switch (direction) {
    case MOTOR_DIR_FORWARD:
        return "FWD";
    case MOTOR_DIR_REVERSE:
        return "REV";
    case MOTOR_DIR_BRAKE:
        return "BRAKE";
    default:
        return "UNKNOWN";
    }
}

static uint8_t Diagnostics_IsCh5Active(uint32_t pulse_us)
{
#if RC_ARM_ACTIVE_HIGH
    return (uint8_t)(pulse_us >= RC_ARM_THRESHOLD_US);
#else
    return (uint8_t)(pulse_us <= 1300U);
#endif
}

static const char *Diagnostics_FaultReason(const DiagnosticsFrame *frame)
{
    if (frame->rc.ch1_valid == 0U) {
        return "invalid-ch1";
    }
    if (frame->rc.ch2_valid == 0U) {
        return "invalid-ch2";
    }
    if (frame->rc.ch5_valid == 0U) {
        return "invalid-ch5";
    }
    if (frame->rc.ch1_fresh == 0U) {
        return "stale-ch1";
    }
    if (frame->rc.ch2_fresh == 0U) {
        return "stale-ch2";
    }
    if (frame->rc.ch5_fresh == 0U) {
        return "stale-ch5";
    }

    switch (frame->safety_state) {
    case SAFETY_ARMED:
        return "none";
    case SAFETY_WAIT_NEUTRAL:
        return "neutral-hold";
    case SAFETY_FAILSAFE:
        return "failsafe";
    case SAFETY_LOCKED:
        return (Diagnostics_IsCh5Active(frame->rc.ch5_us) != 0U) ?
               "locked" : "disarmed";
    default:
        return "state";
    }
}

uint16_t Diagnostics_Format(char *buffer, uint16_t capacity,
                            const DiagnosticsFrame *frame)
{
    int written;
    uint16_t limit;

    if ((buffer == NULL) || (frame == NULL) || (capacity == 0U)) {
        return 0U;
    }

    written = snprintf(buffer, (size_t)capacity,
                       "t=%lu ch1=%lu v=%u f=%u ch2=%lu v=%u f=%u "
                       "ch5=%lu v=%u f=%u state=%s lt=%d rt=%d "
                       "ld=%s lp=%u rd=%s rp=%u fault=%s\r\n",
                       (unsigned long)frame->time_ms,
                       (unsigned long)frame->rc.ch1_us,
                       (unsigned int)frame->rc.ch1_valid,
                       (unsigned int)frame->rc.ch1_fresh,
                       (unsigned long)frame->rc.ch2_us,
                       (unsigned int)frame->rc.ch2_valid,
                       (unsigned int)frame->rc.ch2_fresh,
                       (unsigned long)frame->rc.ch5_us,
                       (unsigned int)frame->rc.ch5_valid,
                       (unsigned int)frame->rc.ch5_fresh,
                       Diagnostics_StateName(frame->safety_state),
                       (int)frame->left_target_permille,
                       (int)frame->right_target_permille,
                       Diagnostics_DirectionName(frame->left_output.direction),
                       (unsigned int)frame->left_output.pwm_permille,
                       Diagnostics_DirectionName(frame->right_output.direction),
                       (unsigned int)frame->right_output.pwm_permille,
                       Diagnostics_FaultReason(frame));
    if (written < 0) {
        buffer[0] = '\0';
        return 0U;
    }

    limit = (uint16_t)(capacity - 1U);
    if ((uint32_t)written > (uint32_t)limit) {
        buffer[limit] = '\0';
        return limit;
    }
    return (uint16_t)written;
}

uint8_t Diagnostics_TryTransmit(const DiagnosticsFrame *frame)
{
    uint16_t length;

    if ((frame == NULL) || (HAL_UART_GetState(&huart1) != HAL_UART_STATE_READY)) {
        return 0U;
    }

    length = Diagnostics_Format(diagnostics_buffer,
                                (uint16_t)sizeof(diagnostics_buffer), frame);
    if (length == 0U) {
        return 0U;
    }
    return (uint8_t)(HAL_UART_Transmit_IT(&huart1,
                                          (uint8_t *)diagnostics_buffer,
                                          length) == HAL_OK);
}
