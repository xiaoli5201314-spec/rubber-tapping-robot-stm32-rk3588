#include "startup_safety.h"

#include "main.h"

static void StartupSafety_BrakeHardware(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* The timer handle may not exist yet. Disable both CC outputs and zero
       their active compare registers directly before changing bridge pins. */
    __HAL_RCC_TIM4_CLK_ENABLE();
    TIM4->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC2E);
    TIM4->CCR1 = 0U;
    TIM4->CCR2 = 0U;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, MOTOR_LEFT_PWM_Pin | MOTOR_RIGHT_PWM_Pin,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, MOTOR_LEFT_IN1_Pin | MOTOR_LEFT_IN2_Pin,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE, MOTOR_RIGHT_IN1_Pin | MOTOR_RIGHT_IN2_Pin,
                      GPIO_PIN_RESET);

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    /* This also covers errors before TIM4 or the bench motor adapter starts. */
    gpio_init.Pin = MOTOR_LEFT_PWM_Pin | MOTOR_RIGHT_PWM_Pin;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    gpio_init.Pin = MOTOR_LEFT_IN1_Pin | MOTOR_LEFT_IN2_Pin;
    HAL_GPIO_Init(GPIOD, &gpio_init);
    gpio_init.Pin = MOTOR_RIGHT_IN1_Pin | MOTOR_RIGHT_IN2_Pin;
    HAL_GPIO_Init(GPIOE, &gpio_init);
}

void StartupSafety_EnterFatal(StartupSafetyFatalHook fatal_hook)
{
    __disable_irq();
    StartupSafety_BrakeHardware();
    if (fatal_hook != 0) {
        fatal_hook();
    }
}
