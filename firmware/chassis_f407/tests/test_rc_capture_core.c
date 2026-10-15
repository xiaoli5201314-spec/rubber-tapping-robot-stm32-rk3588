#include "rc_capture_core.h"
#include "test_support.h"

TEST(valid_1500us_pulse_after_second_period)
{
    RcCaptureCore state;

    RcCaptureCore_Init(&state);
    RcCaptureCore_OnEdge32(&state, 10000U, 1U, 1U);
    RcCaptureCore_OnEdge32(&state, 11500U, 0U, 2U);
    RcCaptureCore_OnEdge32(&state, 30000U, 1U, 20U);
    RcCaptureCore_OnEdge32(&state, 31500U, 0U, 21U);

    ASSERT_EQ_U32(1500U, state.pulse_us);
    ASSERT_EQ_U32(20000U, state.period_us);
    ASSERT_EQ_U32(1U, state.valid);
    ASSERT_EQ_U32(1U, RcCaptureCore_IsFresh(&state, 121U));
}

TEST(rejects_700us_pulse)
{
    RcCaptureCore state;

    RcCaptureCore_Init(&state);
    RcCaptureCore_OnEdge32(&state, 10000U, 1U, 1U);
    RcCaptureCore_OnEdge32(&state, 10700U, 0U, 2U);
    RcCaptureCore_OnEdge32(&state, 30000U, 1U, 20U);
    RcCaptureCore_OnEdge32(&state, 30700U, 0U, 21U);

    ASSERT_EQ_U32(0U, state.pulse_us);
    ASSERT_EQ_U32(0U, state.period_us);
    ASSERT_EQ_U32(0U, state.valid);
    ASSERT_EQ_U32(0U, RcCaptureCore_IsFresh(&state, 21U));
}

TEST(rejects_26000us_period)
{
    RcCaptureCore state;

    RcCaptureCore_Init(&state);
    RcCaptureCore_OnEdge32(&state, 10000U, 1U, 1U);
    RcCaptureCore_OnEdge32(&state, 11500U, 0U, 2U);
    RcCaptureCore_OnEdge32(&state, 36000U, 1U, 26U);
    RcCaptureCore_OnEdge32(&state, 37500U, 0U, 27U);

    ASSERT_EQ_U32(0U, state.pulse_us);
    ASSERT_EQ_U32(0U, state.period_us);
    ASSERT_EQ_U32(0U, state.valid);
    ASSERT_EQ_U32(0U, RcCaptureCore_IsFresh(&state, 27U));
}

TEST(handles_32bit_counter_wrap)
{
    RcCaptureCore state;

    RcCaptureCore_Init(&state);
    RcCaptureCore_OnEdge32(&state, 0xFFFFF000U, 1U, 1U);
    RcCaptureCore_OnEdge32(&state, 0xFFFFF5DCU, 0U, 2U);
    RcCaptureCore_OnEdge32(&state, 0x00003E20U, 1U, 20U);
    RcCaptureCore_OnEdge32(&state, 0x000043FCU, 0U, 21U);

    ASSERT_EQ_U32(1500U, state.pulse_us);
    ASSERT_EQ_U32(20000U, state.period_us);
    ASSERT_EQ_U32(1U, state.valid);
    ASSERT_EQ_U32(1U, RcCaptureCore_IsFresh(&state, 121U));
}

TEST(handles_16bit_counter_wrap)
{
    RcCaptureCore state;

    RcCaptureCore_Init(&state);
    RcCaptureCore_OnEdge16(&state, 65000U, 1U, 1U);
    RcCaptureCore_OnEdge16(&state, 964U, 0U, 2U);
    RcCaptureCore_OnEdge16(&state, 19464U, 1U, 20U);
    RcCaptureCore_OnEdge16(&state, 20964U, 0U, 21U);

    ASSERT_EQ_U32(1500U, state.pulse_us);
    ASSERT_EQ_U32(20000U, state.period_us);
    ASSERT_EQ_U32(1U, state.valid);
    ASSERT_EQ_U32(1U, RcCaptureCore_IsFresh(&state, 121U));
}

TEST(stale_after_101ms)
{
    RcCaptureCore state;

    RcCaptureCore_Init(&state);
    RcCaptureCore_OnEdge32(&state, 10000U, 1U, 1U);
    RcCaptureCore_OnEdge32(&state, 11500U, 0U, 2U);
    RcCaptureCore_OnEdge32(&state, 30000U, 1U, 20U);
    RcCaptureCore_OnEdge32(&state, 31500U, 0U, 21U);

    ASSERT_EQ_U32(1500U, state.pulse_us);
    ASSERT_EQ_U32(20000U, state.period_us);
    ASSERT_EQ_U32(1U, state.valid);
    ASSERT_EQ_U32(0U, RcCaptureCore_IsFresh(&state, 122U));
}

int main(void)
{
    RUN_TEST(valid_1500us_pulse_after_second_period);
    RUN_TEST(rejects_700us_pulse);
    RUN_TEST(rejects_26000us_period);
    RUN_TEST(handles_32bit_counter_wrap);
    RUN_TEST(handles_16bit_counter_wrap);
    RUN_TEST(stale_after_101ms);

    return (test_failures == 0) ? 0 : 1;
}
