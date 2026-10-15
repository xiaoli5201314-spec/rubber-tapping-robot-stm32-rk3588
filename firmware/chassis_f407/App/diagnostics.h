#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>

#include "motor_profile.h"
#include "rc_input.h"
#include "safety.h"

typedef struct {
    uint32_t time_ms;
    RcInputSnapshot rc;
    SafetyState safety_state;
    int16_t left_target_permille;
    int16_t right_target_permille;
    MotorOutput left_output;
    MotorOutput right_output;
} DiagnosticsFrame;

/* Format one complete CRLF-terminated telemetry line. Returns bytes stored. */
uint16_t Diagnostics_Format(char *buffer, uint16_t capacity,
                            const DiagnosticsFrame *frame);

/* Queue one line for USART1 interrupt-driven transmission. Returns non-zero
   when accepted; a busy UART leaves the previous line untouched. */
uint8_t Diagnostics_TryTransmit(const DiagnosticsFrame *frame);

#endif /* DIAGNOSTICS_H */
