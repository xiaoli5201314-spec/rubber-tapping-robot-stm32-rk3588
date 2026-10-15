#ifndef RC_CAPTURE_CORE_H
#define RC_CAPTURE_CORE_H

#include <stdint.h>

typedef struct {
    uint32_t pulse_us;
    uint32_t period_us;
    uint32_t last_valid_ms;
    uint8_t valid;
    uint32_t rise_tick;
    uint32_t previous_rise_tick;
    uint32_t candidate_period;
    uint8_t have_previous_rise;
    uint8_t waiting_for_fall;
    uint8_t have_period;
} RcCaptureCore;

void RcCaptureCore_Init(RcCaptureCore *state);
void RcCaptureCore_OnEdge32(RcCaptureCore *state, uint32_t tick, uint8_t is_rising,
                            uint32_t now_ms);
void RcCaptureCore_OnEdge16(RcCaptureCore *state, uint16_t tick, uint8_t is_rising,
                            uint32_t now_ms);
uint8_t RcCaptureCore_IsFresh(const RcCaptureCore *state, uint32_t now_ms);

#endif
