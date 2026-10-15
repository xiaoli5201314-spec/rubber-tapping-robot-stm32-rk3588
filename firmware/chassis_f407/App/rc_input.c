#include "rc_input.h"

#include "board_config.h"
#include "main.h"
#include "rc_capture_core.h"
#include "tim.h"

static volatile RcCaptureCore rc_ch1;
static volatile RcCaptureCore rc_ch2;
static volatile RcCaptureCore rc_ch5;

static uint8_t RcInput_IsFresh(const RcCaptureCore *state, uint32_t now_ms)
{
    return (uint8_t)((state->valid != 0U) &&
                     ((uint32_t)(now_ms - state->last_valid_ms) <= RC_TIMEOUT_MS));
}

static RcCaptureCore RcInput_CopyCore(volatile const RcCaptureCore *source)
{
    RcCaptureCore copy;

    copy.pulse_us = source->pulse_us;
    copy.period_us = source->period_us;
    copy.last_valid_ms = source->last_valid_ms;
    copy.valid = source->valid;
    copy.rise_tick = source->rise_tick;
    copy.previous_rise_tick = source->previous_rise_tick;
    copy.candidate_period = source->candidate_period;
    copy.have_previous_rise = source->have_previous_rise;
    copy.waiting_for_fall = source->waiting_for_fall;
    copy.have_period = source->have_period;
    return copy;
}

static uint8_t RcInput_HandleCaptureAmbiguity(TIM_HandleTypeDef *htim,
                                              uint32_t capture_flag,
                                              uint32_t overcapture_flag,
                                              uint32_t channel)
{
    uint32_t flags = capture_flag | overcapture_flag;

    if ((htim->Instance->SR & flags) == 0U) {
        return 0U;
    }

    /* HAL_TIM_IRQHandler clears the original CCxIF before this callback, but
       a new edge can assert CCxIF while we sample CCR/GPIO.  CCxOF is also
       possible.  Either condition makes timestamp and polarity ambiguous. */
    __HAL_TIM_CLEAR_FLAG(htim, flags);
    RcInput_InvalidateOvercapture(channel);
    return 1U;
}

void RcInput_Init(void)
{
    RcCaptureCore_Init((RcCaptureCore *)&rc_ch1);
    RcCaptureCore_Init((RcCaptureCore *)&rc_ch2);
    RcCaptureCore_Init((RcCaptureCore *)&rc_ch5);
}

HAL_StatusTypeDef RcInput_Start(void)
{
    HAL_StatusTypeDef status;

    status = HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3);
    if (status != HAL_OK) {
        return status;
    }
    status = HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4);
    if (status != HAL_OK) {
        return status;
    }
    status = HAL_TIM_IC_Start_IT(&htim10, TIM_CHANNEL_1);
    return status;
}

void RcInput_GetSnapshot(RcInputSnapshot *snapshot)
{
    RcCaptureCore ch1;
    RcCaptureCore ch2;
    RcCaptureCore ch5;
    uint32_t now_ms;
    uint32_t primask;

    if (snapshot == NULL) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    ch1 = RcInput_CopyCore(&rc_ch1);
    ch2 = RcInput_CopyCore(&rc_ch2);
    ch5 = RcInput_CopyCore(&rc_ch5);
    if (primask == 0U) {
        __enable_irq();
    }

    now_ms = HAL_GetTick();
    snapshot->ch1_us = ch1.pulse_us;
    snapshot->ch2_us = ch2.pulse_us;
    snapshot->ch5_us = ch5.pulse_us;
    snapshot->ch1_period_us = ch1.period_us;
    snapshot->ch2_period_us = ch2.period_us;
    snapshot->ch5_period_us = ch5.period_us;
    snapshot->ch1_valid = ch1.valid;
    snapshot->ch2_valid = ch2.valid;
    snapshot->ch5_valid = ch5.valid;
    snapshot->ch1_fresh = RcInput_IsFresh(&ch1, now_ms);
    snapshot->ch2_fresh = RcInput_IsFresh(&ch2, now_ms);
    snapshot->ch5_fresh = RcInput_IsFresh(&ch5, now_ms);
}

void RcInput_InvalidateOvercapture(uint32_t channel)
{
    volatile RcCaptureCore *state = NULL;

    if (channel == RC_INPUT_CHANNEL_CH1) {
        state = &rc_ch1;
    } else if (channel == RC_INPUT_CHANNEL_CH2) {
        state = &rc_ch2;
    } else if (channel == RC_INPUT_CHANNEL_CH5) {
        state = &rc_ch5;
    }

    if (state != NULL) {
        state->valid = 0U;
        state->waiting_for_fall = 0U;
        state->have_previous_rise = 0U;
        state->have_period = 0U;
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    uint32_t captured;
    uint8_t is_rising;
    uint32_t now_ms = HAL_GetTick();

    if (htim == &htim2) {
        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
            if (RcInput_HandleCaptureAmbiguity(htim, TIM_FLAG_CC3,
                                               TIM_FLAG_CC3OF,
                                               RC_INPUT_CHANNEL_CH1) != 0U) {
                return;
            }
            captured = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
            is_rising = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_SET) ? 1U : 0U;
            if (RcInput_HandleCaptureAmbiguity(htim, TIM_FLAG_CC3,
                                               TIM_FLAG_CC3OF,
                                               RC_INPUT_CHANNEL_CH1) != 0U) {
                return;
            }
            RcCaptureCore_OnEdge32((RcCaptureCore *)&rc_ch1, captured, is_rising, now_ms);
        } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4) {
            if (RcInput_HandleCaptureAmbiguity(htim, TIM_FLAG_CC4,
                                               TIM_FLAG_CC4OF,
                                               RC_INPUT_CHANNEL_CH2) != 0U) {
                return;
            }
            captured = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_4);
            is_rising = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == GPIO_PIN_SET) ? 1U : 0U;
            if (RcInput_HandleCaptureAmbiguity(htim, TIM_FLAG_CC4,
                                               TIM_FLAG_CC4OF,
                                               RC_INPUT_CHANNEL_CH2) != 0U) {
                return;
            }
            RcCaptureCore_OnEdge32((RcCaptureCore *)&rc_ch2, captured, is_rising, now_ms);
        }
    } else if (htim == &htim10 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        if (RcInput_HandleCaptureAmbiguity(htim, TIM_FLAG_CC1,
                                           TIM_FLAG_CC1OF,
                                           RC_INPUT_CHANNEL_CH5) != 0U) {
            return;
        }
        captured = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        is_rising = (HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_6) == GPIO_PIN_SET) ? 1U : 0U;
        if (RcInput_HandleCaptureAmbiguity(htim, TIM_FLAG_CC1,
                                           TIM_FLAG_CC1OF,
                                           RC_INPUT_CHANNEL_CH5) != 0U) {
            return;
        }
        RcCaptureCore_OnEdge16((RcCaptureCore *)&rc_ch5, (uint16_t)captured, is_rising, now_ms);
    }
}
