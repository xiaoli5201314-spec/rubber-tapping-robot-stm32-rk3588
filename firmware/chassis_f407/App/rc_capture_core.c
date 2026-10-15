#include "rc_capture_core.h"

#include <string.h>

#define RC_CAPTURE_MIN_PULSE_US 800U
#define RC_CAPTURE_MAX_PULSE_US 2200U
#define RC_CAPTURE_MIN_PERIOD_US 15000U
#define RC_CAPTURE_MAX_PERIOD_US 25000U
#define RC_CAPTURE_FRESHNESS_MS 100U

static uint32_t RcCaptureCore_TickDifference(uint32_t newer_tick, uint32_t older_tick,
                                             uint32_t counter_mask)
{
    return (newer_tick - older_tick) & counter_mask;
}

static void RcCaptureCore_OnEdge(RcCaptureCore *state, uint32_t tick, uint8_t is_rising,
                                 uint32_t now_ms, uint32_t counter_mask)
{
    uint32_t width_us;

    if (is_rising != 0U) {
        if (state->have_previous_rise != 0U) {
            state->candidate_period = RcCaptureCore_TickDifference(
                tick, state->previous_rise_tick, counter_mask);
            state->have_period = 1U;
        }

        state->previous_rise_tick = tick;
        state->rise_tick = tick;
        state->have_previous_rise = 1U;
        state->waiting_for_fall = 1U;
        return;
    }

    if (state->waiting_for_fall == 0U) {
        return;
    }

    state->waiting_for_fall = 0U;
    if (state->have_period == 0U) {
        return;
    }

    width_us = RcCaptureCore_TickDifference(tick, state->rise_tick, counter_mask);
    if ((width_us >= RC_CAPTURE_MIN_PULSE_US) &&
        (width_us <= RC_CAPTURE_MAX_PULSE_US) &&
        (state->candidate_period >= RC_CAPTURE_MIN_PERIOD_US) &&
        (state->candidate_period <= RC_CAPTURE_MAX_PERIOD_US)) {
        state->pulse_us = width_us;
        state->period_us = state->candidate_period;
        state->last_valid_ms = now_ms;
        state->valid = 1U;
    } else {
        state->valid = 0U;
    }
}

void RcCaptureCore_Init(RcCaptureCore *state)
{
    (void)memset(state, 0, sizeof(*state));
}

void RcCaptureCore_OnEdge32(RcCaptureCore *state, uint32_t tick, uint8_t is_rising,
                            uint32_t now_ms)
{
    RcCaptureCore_OnEdge(state, tick, is_rising, now_ms, UINT32_MAX);
}

void RcCaptureCore_OnEdge16(RcCaptureCore *state, uint16_t tick, uint8_t is_rising,
                            uint32_t now_ms)
{
    RcCaptureCore_OnEdge(state, (uint32_t)tick, is_rising, now_ms, UINT16_MAX);
}

uint8_t RcCaptureCore_IsFresh(const RcCaptureCore *state, uint32_t now_ms)
{
    return (uint8_t)((state->valid != 0U) &&
                     ((uint32_t)(now_ms - state->last_valid_ms) <= RC_CAPTURE_FRESHNESS_MS));
}
