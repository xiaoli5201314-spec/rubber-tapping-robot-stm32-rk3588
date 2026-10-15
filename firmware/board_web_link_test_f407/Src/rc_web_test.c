#include "rc_web_test.h"
#include "board.h"
#include "board_config.h"
#include "rc_capture_core.h"
#include "safety.h"
#include "chassis_math.h"
#include "motor_profile.h"
#include "test_config.h"
#if WEB_MOTOR_TEST
#include "motor_web.h"
#endif
#include "stm32f4xx.h"
#include <string.h>

static volatile RcCaptureCore s_capture[3];
static SafetyStateMachine s_safety;
static MotorProfile s_left_profile;
static MotorProfile s_right_profile;
static RcWebSnapshot s_snapshot;
static uint32_t s_last_profile;
#if WEB_MOTOR_TEST
static volatile uint8_t s_control_ready;
static uint32_t s_last_tick;
#endif

/* State is shared only by timer interrupts and an IRQ-masked snapshot. */
static void capture_edge(TIM_TypeDef *timer, volatile uint32_t *ccr,
                         GPIO_TypeDef *gpio, uint32_t pin, uint32_t flags,
                         uint32_t overcapture, uint32_t status, uint32_t index)
{
    RcCaptureCore capture;
    uint32_t tick;
    uint8_t high;
    if ((status & flags) == 0U) return;
    /* Clear before sampling: a new edge during CCR/GPIO reads reasserts CCIF
       and is rejected instead of pairing an old timestamp with a new level. */
    timer->SR = ~flags;
    capture = s_capture[index];
    tick = *ccr;
    high = (uint8_t)((gpio->IDR & (1UL << pin)) != 0U);
    if ((status & overcapture) != 0U || (timer->SR & flags) != 0U)
    {
        timer->SR = ~flags;
        RcCaptureCore_Init(&capture);
    }
    else if (index == 2U)
        RcCaptureCore_OnEdge16(&capture, (uint16_t)tick, high, Board_Millis());
    else
        RcCaptureCore_OnEdge32(&capture, tick, high, Board_Millis());
    s_capture[index] = capture;
}

void TIM2_IRQHandler(void)
{
    const uint32_t status = TIM2->SR;
    capture_edge(TIM2, &TIM2->CCR3, GPIOB, 10U,
                 TIM_SR_CC3IF | TIM_SR_CC3OF, TIM_SR_CC3OF, status, 0U);
    capture_edge(TIM2, &TIM2->CCR4, GPIOB, 11U,
                 TIM_SR_CC4IF | TIM_SR_CC4OF, TIM_SR_CC4OF, status, 1U);
}

void TIM1_UP_TIM10_IRQHandler(void)
{
    capture_edge(TIM10, &TIM10->CCR1, GPIOF, 6U,
                 TIM_SR_CC1IF | TIM_SR_CC1OF, TIM_SR_CC1OF, TIM10->SR, 2U);
}

void RcWeb_Init(void)
{
    uint32_t i;
    RcCaptureCore empty;
#if WEB_MOTOR_TEST
    s_control_ready = 0U;
#endif
    RcCaptureCore_Init(&empty);
    for (i = 0U; i < 3U; ++i) s_capture[i] = empty;
    Safety_Init(&s_safety);
    (void)memset(&s_left_profile, 0, sizeof(s_left_profile));
    (void)memset(&s_right_profile, 0, sizeof(s_right_profile));
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = "LOCKED";
    s_last_profile = 0U;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOFEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM10EN;
    (void)RCC->APB2ENR;
    __DSB();
    GPIOB->MODER = (GPIOB->MODER & ~((3UL << 20) | (3UL << 22))) |
                   (2UL << 20) | (2UL << 22);
    GPIOB->PUPDR &= ~((3UL << 20) | (3UL << 22));
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~((15UL << 8) | (15UL << 12))) |
                    (1UL << 8) | (1UL << 12);
    GPIOF->MODER = (GPIOF->MODER & ~(3UL << 12)) | (2UL << 12);
    GPIOF->PUPDR &= ~(3UL << 12);
    GPIOF->AFR[0] = (GPIOF->AFR[0] & ~(15UL << 24)) | (3UL << 24);

    /* This standalone project runs HSI 16 MHz, APB1/APB2 /1.
       Both timers therefore use PSC=15 for a 1 us counter tick. */
    TIM2->CR1 = 0U;
    TIM2->DIER = 0U;
    TIM2->CCER = 0U;
    TIM2->PSC = SystemCoreClock / 1000000U - 1U;
    TIM2->ARR = UINT32_MAX;
    TIM2->CCMR2 = (1UL << 0) | (8UL << 4) |
                  (1UL << 8) | (8UL << 12); /* direct TI, filter=8 */
    TIM2->CCER = TIM_CCER_CC3E | TIM_CCER_CC3P | TIM_CCER_CC3NP |
                  TIM_CCER_CC4E | TIM_CCER_CC4P | TIM_CCER_CC4NP;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    TIM2->DIER = TIM_DIER_CC3IE | TIM_DIER_CC4IE;

    TIM10->CR1 = 0U;
    TIM10->DIER = 0U;
    TIM10->CCER = 0U;
    TIM10->PSC = SystemCoreClock / 1000000U - 1U;
    TIM10->ARR = UINT16_MAX;
    TIM10->CCMR1 = (1UL << 0) | (8UL << 4);
    TIM10->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP;
    TIM10->EGR = TIM_EGR_UG;
    TIM10->SR = 0U;
    TIM10->DIER = TIM_DIER_CC1IE;
    /* Higher urgency than ESP RX; keep interrupts short. TIM1 is unused. */
    NVIC_SetPriority(TIM2_IRQn, 0U);
    NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 0U);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_ClearPendingIRQ(TIM1_UP_TIM10_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
    TIM10->CR1 = TIM_CR1_CEN;
#if WEB_MOTOR_TEST
    s_last_tick = Board_Millis();
    if (MotorWeb_Init()) s_control_ready = 1U;
    else s_snapshot.state = "FAILSAFE";
#endif
}

static int16_t signed_output(MotorOutput output)
{
    if (output.direction == MOTOR_DIR_FORWARD)
        return (int16_t)output.pwm_permille;
    if (output.direction == MOTOR_DIR_REVERSE)
        return -(int16_t)output.pwm_permille;
    return 0;
}

void RcWeb_Service(uint32_t now_ms)
{
    RcCaptureCore capture[3];
    SafetyInputs inputs;
    ChassisCommand target = {0, 0};
    uint32_t i;
    uint8_t allowed;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < 3U; ++i) capture[i] = s_capture[i];
    /* Refresh the caller's time after copying: an IRQ may have timestamped
       a new pulse in the next millisecond before we masked interrupts. */
    now_ms = Board_Millis();
    __set_PRIMASK(primask);
    for (i = 0U; i < 3U; ++i)
    {
        s_snapshot.valid[i] = RcCaptureCore_IsFresh(&capture[i], now_ms);
        s_snapshot.pulse[i] = s_snapshot.valid[i] ? capture[i].pulse_us : 0U;
        s_snapshot.period[i] = s_snapshot.valid[i] ? capture[i].period_us : 0U;
    }
    inputs.ch1_fresh = s_snapshot.valid[0];
    inputs.ch2_fresh = s_snapshot.valid[1];
    inputs.ch5_fresh = s_snapshot.valid[2];
    inputs.ch1_us = s_snapshot.pulse[0];
    inputs.ch2_us = s_snapshot.pulse[1];
    inputs.ch5_us = s_snapshot.pulse[2];
    Safety_Update(&s_safety, &inputs, now_ms);
    switch (s_safety.state)
    {
        case SAFETY_WAIT_NEUTRAL: s_snapshot.state = "WAIT"; break;
        case SAFETY_ARMED: s_snapshot.state = "ARMED"; break;
        case SAFETY_FAILSAFE: s_snapshot.state = "FAILSAFE"; break;
        default: s_snapshot.state = "LOCKED"; break;
    }
    allowed = Safety_IsDriveAllowed(&s_safety);
    if (allowed)
    {
        target = ChassisMath_Mix(
            ChassisMath_PulseToCommand(inputs.ch2_us, RC_CENTER_US,
                                      RC_DEADBAND_US, RC_THROTTLE_INVERT),
            ChassisMath_PulseToCommand(inputs.ch1_us, RC_CENTER_US,
                                      RC_DEADBAND_US, RC_STEERING_INVERT));
        target.left = (int16_t)((int32_t)target.left *
                               MOTOR_MAX_OUTPUT_PERMILLE / 1000);
        target.right = (int16_t)((int32_t)target.right *
                                MOTOR_MAX_OUTPUT_PERMILLE / 1000);
    }
    if (!allowed || (uint32_t)(now_ms - s_last_profile) >= 10U)
    {
        s_last_profile = now_ms;
        s_snapshot.left = signed_output(MotorProfile_Update(
            &s_left_profile, target.left, now_ms, (uint8_t)!allowed));
        s_snapshot.right = signed_output(MotorProfile_Update(
            &s_right_profile, target.right, now_ms, (uint8_t)!allowed));
    }
#if WEB_MOTOR_TEST
    if (s_control_ready)
        MotorWeb_Apply(s_snapshot.left, s_snapshot.right);
#endif
}

void RcWeb_Tick(uint32_t now_ms)
{
#if WEB_MOTOR_TEST
    if (s_control_ready && (uint32_t)(now_ms - s_last_tick) >= 10U)
    {
        s_last_tick = now_ms;
        RcWeb_Service(now_ms);
        MotorWeb_FeedWatchdog();
    }
#else
    (void)now_ms;
#endif
}

void RcWeb_GetSnapshot(RcWebSnapshot *snapshot)
{
#if WEB_MOTOR_TEST
    /* The 10 ms control ISR is the sole writer, foreground only copies. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *snapshot = s_snapshot;
    __set_PRIMASK(primask);
#else
    RcWeb_Service(Board_Millis());
    *snapshot = s_snapshot;
#endif
}
