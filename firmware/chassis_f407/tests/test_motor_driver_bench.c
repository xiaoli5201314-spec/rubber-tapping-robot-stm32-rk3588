#include <string.h>

#include "main.h"
#include "motor_driver.h"
#include "test_support.h"
#include "tim.h"

GPIO_TypeDef fake_gpiob;
GPIO_TypeDef fake_gpiod;
GPIO_TypeDef fake_gpioe;
static TIM_TypeDef fake_tim4;
TIM_HandleTypeDef htim4 = {&fake_tim4, 0U};
static uint32_t timer_writes;
static uint32_t high_pin_writes;
static uint32_t bad_gpio_initializations;

void FakeHal_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t compare)
{
    (void)timer;
    (void)channel;
    (void)compare;
    timer_writes++;
}

void TIM_CCxChannelCmd(TIM_TypeDef *timer, uint32_t channel, uint32_t state)
{
    (void)timer;
    (void)channel;
    (void)state;
    timer_writes++;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) {
        high_pin_writes++;
        port->ODR |= pin;
    } else {
        port->ODR &= ~(uint32_t)pin;
    }
}

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config)
{
    if ((config->Mode != GPIO_MODE_OUTPUT_PP) ||
        (config->Pull != GPIO_NOPULL) ||
        (port->clock_enabled == 0U) ||
        ((port->ODR & config->Pin) != 0U)) {
        bad_gpio_initializations++;
    }
    port->output_pins |= config->Pin;
}

static void ResetHardware(void)
{
    (void)memset(&fake_gpiob, 0, sizeof(fake_gpiob));
    (void)memset(&fake_gpiod, 0, sizeof(fake_gpiod));
    (void)memset(&fake_gpioe, 0, sizeof(fake_gpioe));
    (void)memset(&fake_tim4, 0, sizeof(fake_tim4));
    fake_gpiob.ODR = GPIO_PIN_6 | GPIO_PIN_7;
    fake_gpiod.ODR = GPIO_PIN_6 | GPIO_PIN_7;
    fake_gpioe.ODR = GPIO_PIN_5 | GPIO_PIN_6;
    timer_writes = 0U;
    high_pin_writes = 0U;
    bad_gpio_initializations = 0U;
}

/* Catch accidentally leaving PB6/PB7 floating or in timer alternate mode. */
TEST(bench_initialization_latches_all_six_outputs_low_before_output_mode)
{
    ResetHardware();
    MotorDriver_Init();

    ASSERT_EQ_U32(0U, bad_gpio_initializations);
    ASSERT_EQ_U32(0U, fake_gpiob.ODR);
    ASSERT_EQ_U32(0U, fake_gpiod.ODR);
    ASSERT_EQ_U32(0U, fake_gpioe.ODR);
    ASSERT_EQ_U32(GPIO_PIN_6 | GPIO_PIN_7, fake_gpiob.output_pins);
    ASSERT_EQ_U32(GPIO_PIN_6 | GPIO_PIN_7, fake_gpiod.output_pins);
    ASSERT_EQ_U32(GPIO_PIN_5 | GPIO_PIN_6, fake_gpioe.output_pins);
    ASSERT_EQ_U32(0U, timer_writes);
}

/* Catch accidentally using the physical motor adapter for simulated commands. */
TEST(bench_forward_and_reverse_never_raise_physical_outputs)
{
    MotorOutput output = {MOTOR_DIR_FORWARD, 300U};

    ResetHardware();
    MotorDriver_Init();
    MotorDriver_ApplyLeft(output);
    MotorDriver_ApplyRight(output);
    output.direction = MOTOR_DIR_REVERSE;
    output.pwm_permille = 1000U;
    MotorDriver_ApplyLeft(output);
    MotorDriver_ApplyRight(output);
    MotorDriver_EmergencyBrake();

    ASSERT_EQ_U32(0U, high_pin_writes);
    ASSERT_EQ_U32(0U, timer_writes);
    ASSERT_EQ_U32(0U, fake_gpiob.ODR);
    ASSERT_EQ_U32(0U, fake_gpiod.ODR);
    ASSERT_EQ_U32(0U, fake_gpioe.ODR);
}

int main(void)
{
    RUN_TEST(bench_initialization_latches_all_six_outputs_low_before_output_mode);
    RUN_TEST(bench_forward_and_reverse_never_raise_physical_outputs);
    return test_failures;
}
