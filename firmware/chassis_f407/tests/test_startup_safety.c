#include <stdio.h>

#include "startup_safety.h"
#include "test_support.h"
#include "main.h"

GPIO_TypeDef fake_gpiob;
GPIO_TypeDef fake_gpiod;
GPIO_TypeDef fake_gpioe;
TIM_TypeDef fake_tim4;

static uint8_t gpiob_clock_enabled;
static uint8_t gpiod_clock_enabled;
static uint8_t gpioe_clock_enabled;
static uint8_t tim4_clock_enabled;
static uint8_t interrupts_disabled;
static uint8_t fatal_entered;
static uint8_t hardware_safe_at_fatal_entry;

void FakeHal_EnableGpiobClock(void)
{
    gpiob_clock_enabled = 1U;
}

void FakeHal_EnableGpiodClock(void)
{
    gpiod_clock_enabled = 1U;
}

void FakeHal_EnableGpioeClock(void)
{
    gpioe_clock_enabled = 1U;
}

void FakeHal_EnableTim4Clock(void)
{
    tim4_clock_enabled = 1U;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) {
        port->ODR |= pins;
    } else {
        port->ODR &= ~(uint32_t)pins;
    }
}

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init)
{
    if ((init->Mode == GPIO_MODE_OUTPUT_PP) &&
        (init->Pull == GPIO_NOPULL) &&
        (init->Speed == GPIO_SPEED_FREQ_LOW)) {
        port->output_mask |= init->Pin;
    }
}

void __disable_irq(void)
{
    interrupts_disabled = 1U;
}

static uint8_t HardwareIsSafelyBraked(void)
{
    uint32_t pwm_pins = MOTOR_LEFT_PWM_Pin | MOTOR_RIGHT_PWM_Pin;
    uint32_t left_pins = MOTOR_LEFT_IN1_Pin | MOTOR_LEFT_IN2_Pin;
    uint32_t right_pins = MOTOR_RIGHT_IN1_Pin | MOTOR_RIGHT_IN2_Pin;

    return (uint8_t)((gpiob_clock_enabled != 0U) &&
                     (gpiod_clock_enabled != 0U) &&
                     (gpioe_clock_enabled != 0U) &&
                     (tim4_clock_enabled != 0U) &&
                     ((fake_tim4.CCER & (TIM_CCER_CC1E | TIM_CCER_CC2E)) == 0U) &&
                     (fake_tim4.CCR1 == 0U) &&
                     (fake_tim4.CCR2 == 0U) &&
                     ((fake_gpiob.ODR & pwm_pins) == 0U) &&
                     ((fake_gpiod.ODR & left_pins) == 0U) &&
                     ((fake_gpioe.ODR & right_pins) == 0U) &&
                     ((fake_gpiob.output_mask & pwm_pins) == pwm_pins) &&
                     ((fake_gpiod.output_mask & left_pins) == left_pins) &&
                     ((fake_gpioe.output_mask & right_pins) == right_pins));
}

static void RecordFatalEntry(void)
{
    hardware_safe_at_fatal_entry = HardwareIsSafelyBraked();
    fatal_entered = 1U;
}

/* Catch early errors leaving PWM pins floating before the bench adapter runs. */
TEST(clock_failure_before_gpio_drives_all_six_motor_pins_low_before_fatal_path)
{
    uint32_t pwm_pins = MOTOR_LEFT_PWM_Pin | MOTOR_RIGHT_PWM_Pin;
    uint32_t left_pins = MOTOR_LEFT_IN1_Pin | MOTOR_LEFT_IN2_Pin;
    uint32_t right_pins = MOTOR_RIGHT_IN1_Pin | MOTOR_RIGHT_IN2_Pin;

    gpiob_clock_enabled = 0U;
    gpiod_clock_enabled = 0U;
    gpioe_clock_enabled = 0U;
    tim4_clock_enabled = 0U;
    interrupts_disabled = 0U;
    fatal_entered = 0U;
    hardware_safe_at_fatal_entry = 0U;
    fake_gpiob.ODR = pwm_pins;
    fake_gpiob.output_mask = 0U;
    fake_gpiod.ODR = left_pins;
    fake_gpiod.output_mask = 0U;
    fake_gpioe.ODR = right_pins;
    fake_gpioe.output_mask = 0U;
    fake_tim4.CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    fake_tim4.CCR1 = 75U;
    fake_tim4.CCR2 = 80U;

    StartupSafety_EnterFatal(RecordFatalEntry);

    ASSERT_EQ_U32(1U, interrupts_disabled);
    ASSERT_EQ_U32(1U, fatal_entered);
    ASSERT_EQ_U32(1U, hardware_safe_at_fatal_entry);
}

int main(void)
{
    RUN_TEST(clock_failure_before_gpio_drives_all_six_motor_pins_low_before_fatal_path);

    if (test_failures != 0) {
        (void)fprintf(stderr, "%d test(s) failed\n", test_failures);
        return 1;
    }
    (void)printf("All startup safety tests passed\n");
    return 0;
}
