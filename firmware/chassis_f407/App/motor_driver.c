#include "motor_driver.h"

#include "board_config.h"
#include "main.h"
#include "tim.h"

#if RC_BENCH_TEST

/* Bench commands still pass through mixing/profile logic for display, but
   never reach TIM4 or physical direction outputs. */
void MotorDriver_EmergencyBrake(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_RESET);
}

void MotorDriver_Init(void)
{
    GPIO_InitTypeDef config = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    MotorDriver_EmergencyBrake();

    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;
    config.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &config);
    HAL_GPIO_Init(GPIOD, &config);
    config.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOE, &config);
}

void MotorDriver_ApplyLeft(MotorOutput output)
{
    (void)output;
    MotorDriver_EmergencyBrake();
}

void MotorDriver_ApplyRight(MotorOutput output)
{
    (void)output;
    MotorDriver_EmergencyBrake();
}

#else

static uint32_t MotorDriver_ScalePwm(uint16_t pwm_permille)
{
    if (pwm_permille > 1000U) {
        pwm_permille = 1000U;
    }
    return ((uint32_t)pwm_permille * 100U + 500U) / 1000U;
}

static uint32_t MotorDriver_PreLoadEnabled(TIM_HandleTypeDef *timer, uint32_t channel)
{
    if (channel == TIM_CHANNEL_1) {
        return timer->Instance->CCMR1 & TIM_CCMR1_OC1PE;
    }
    return timer->Instance->CCMR1 & TIM_CCMR1_OC2PE;
}

static uint32_t MotorDriver_DisablePwmForDirectionChange(TIM_HandleTypeDef *timer,
                                                          uint32_t channel)
{
    uint32_t preload_enabled = MotorDriver_PreLoadEnabled(timer, channel);

    /* Disable only this output channel.  Do not stop the shared TIM4 counter
       because the other motor can still be driving. */
    TIM_CCxChannelCmd(timer->Instance, channel, TIM_CCx_DISABLE);
    if (preload_enabled != 0U) {
        __HAL_TIM_DISABLE_OCxPRELOAD(timer, channel);
    }

    /* With preload disabled this is an immediate active-register zero, rather
       than a shadow value that could be overwritten before the next update. */
    __HAL_TIM_SET_COMPARE(timer, channel, 0U);
    return preload_enabled;
}

static void MotorDriver_RestorePwmAfterDirectionChange(TIM_HandleTypeDef *timer,
                                                        uint32_t channel,
                                                        uint32_t preload_enabled,
                                                        uint32_t compare)
{
    /* Preload remains off here, so the new physical duty is active before the
       output pin is enabled again. */
    __HAL_TIM_SET_COMPARE(timer, channel, compare);
    if (preload_enabled != 0U) {
        __HAL_TIM_ENABLE_OCxPRELOAD(timer, channel);
    }
    if (compare != 0U) {
        TIM_CCxChannelCmd(timer->Instance, channel, TIM_CCx_ENABLE);
    }
}

static void MotorDriver_Apply(TIM_HandleTypeDef *timer, uint32_t channel,
                              GPIO_TypeDef *in1_port, uint16_t in1_pin,
                              GPIO_TypeDef *in2_port, uint16_t in2_pin,
                              MotorOutput output)
{
    GPIO_PinState in1 = GPIO_PIN_RESET;
    GPIO_PinState in2 = GPIO_PIN_RESET;
    uint32_t compare = 0U;
    uint32_t preload_enabled;

    if (output.direction == MOTOR_DIR_FORWARD) {
        compare = MotorDriver_ScalePwm(output.pwm_permille);
        if (compare != 0U) {
            in1 = GPIO_PIN_SET;
        }
    } else if (output.direction == MOTOR_DIR_REVERSE) {
        compare = MotorDriver_ScalePwm(output.pwm_permille);
        if (compare != 0U) {
            in2 = GPIO_PIN_SET;
        }
    }

    preload_enabled = MotorDriver_DisablePwmForDirectionChange(timer, channel);
    HAL_GPIO_WritePin(in1_port, in1_pin, in1);
    HAL_GPIO_WritePin(in2_port, in2_pin, in2);
    MotorDriver_RestorePwmAfterDirectionChange(timer, channel, preload_enabled, compare);
}

void MotorDriver_Init(void)
{
    MotorDriver_EmergencyBrake();
}

void MotorDriver_ApplyLeft(MotorOutput output)
{
    MotorDriver_Apply(&htim4, TIM_CHANNEL_1,
                      GPIOD, GPIO_PIN_6, GPIOD, GPIO_PIN_7, output);
}

void MotorDriver_ApplyRight(MotorOutput output)
{
    MotorDriver_Apply(&htim4, TIM_CHANNEL_2,
                      GPIOE, GPIO_PIN_5, GPIOE, GPIO_PIN_6, output);
}

void MotorDriver_EmergencyBrake(void)
{
    uint32_t ch1_preload = MotorDriver_DisablePwmForDirectionChange(&htim4, TIM_CHANNEL_1);
    uint32_t ch2_preload = MotorDriver_DisablePwmForDirectionChange(&htim4, TIM_CHANNEL_2);

    if (ch1_preload != 0U) {
        __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, TIM_CHANNEL_1);
    }
    if (ch2_preload != 0U) {
        __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, TIM_CHANNEL_2);
    }
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_RESET);
}

#endif
