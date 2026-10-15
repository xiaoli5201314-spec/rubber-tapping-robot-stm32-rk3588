#include <string.h>

#include "main.h"
#include "motor_driver.h"
#include "test_support.h"
#include "tim.h"

GPIO_TypeDef fake_gpiod;
GPIO_TypeDef fake_gpioe;
static TIM_TypeDef fake_tim4;
TIM_HandleTypeDef htim4 = { &fake_tim4, 0U };

static uint32_t fake_active_ccr1;
static uint32_t fake_active_ccr2;
static uint32_t fake_shadow_ccr1;
static uint32_t fake_shadow_ccr2;
static uint32_t direction_write_while_left_pwm_active;

void FakeHal_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t compare)
{
    if (channel == TIM_CHANNEL_1) {
        fake_shadow_ccr1 = compare;
        if ((timer->Instance->CCMR1 & TIM_CCMR1_OC1PE) == 0U) {
            fake_active_ccr1 = compare;
        }
    } else if (channel == TIM_CHANNEL_2) {
        fake_shadow_ccr2 = compare;
        if ((timer->Instance->CCMR1 & TIM_CCMR1_OC2PE) == 0U) {
            fake_active_ccr2 = compare;
        }
    }
}

void TIM_CCxChannelCmd(TIM_TypeDef *timer, uint32_t channel, uint32_t state)
{
    uint32_t mask = (channel == TIM_CHANNEL_1) ? TIM_CCER_CC1E : TIM_CCER_CC2E;
    if (state == TIM_CCx_ENABLE) {
        timer->CCER |= mask;
    } else {
        timer->CCER &= ~mask;
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if ((port == GPIOD) && ((pin == GPIO_PIN_6) || (pin == GPIO_PIN_7)) &&
        ((fake_tim4.CCER & TIM_CCER_CC1E) != 0U) && (fake_active_ccr1 != 0U)) {
        direction_write_while_left_pwm_active++;
    }

    if (state == GPIO_PIN_SET) {
        port->ODR |= pin;
    } else {
        port->ODR &= ~(uint32_t)pin;
    }
}

static void ResetFakeHardware(void)
{
    (void)memset(&fake_gpiod, 0, sizeof(fake_gpiod));
    (void)memset(&fake_gpioe, 0, sizeof(fake_gpioe));
    (void)memset(&fake_tim4, 0, sizeof(fake_tim4));
    fake_tim4.CCMR1 = TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE;
    fake_tim4.CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    fake_active_ccr1 = 80U;
    fake_active_ccr2 = 35U;
    fake_shadow_ccr1 = fake_active_ccr1;
    fake_shadow_ccr2 = fake_active_ccr2;
    direction_write_while_left_pwm_active = 0U;
}

TEST(disables_left_physical_pwm_before_direction_with_preload)
{
    MotorOutput output;

    ResetFakeHardware();
    output.direction = MOTOR_DIR_REVERSE;
    output.pwm_permille = 500U;
    MotorDriver_ApplyLeft(output);

    ASSERT_EQ_U32(0U, direction_write_while_left_pwm_active);
    ASSERT_EQ_U32(50U, fake_active_ccr1);
    ASSERT_EQ_U32(50U, fake_shadow_ccr1);
    ASSERT_EQ_U32(35U, fake_active_ccr2);
    ASSERT_EQ_U32(35U, fake_shadow_ccr2);
    ASSERT_EQ_U32(TIM_CCER_CC1E | TIM_CCER_CC2E, fake_tim4.CCER);
    ASSERT_EQ_U32(0U, fake_gpiod.ODR & GPIO_PIN_6);
    ASSERT_EQ_U32(GPIO_PIN_7, fake_gpiod.ODR & GPIO_PIN_7);
}

TEST(zero_scaled_pwm_forces_brake_direction_and_no_pwm)
{
    MotorOutput output;

    ResetFakeHardware();
    output.direction = MOTOR_DIR_FORWARD;
    output.pwm_permille = 1U;
    MotorDriver_ApplyLeft(output);

    ASSERT_EQ_U32(0U, direction_write_while_left_pwm_active);
    ASSERT_EQ_U32(0U, fake_active_ccr1);
    ASSERT_EQ_U32(0U, fake_shadow_ccr1);
    ASSERT_EQ_U32(TIM_CCER_CC2E, fake_tim4.CCER);
    ASSERT_EQ_U32(0U, fake_gpiod.ODR & GPIO_PIN_6);
    ASSERT_EQ_U32(0U, fake_gpiod.ODR & GPIO_PIN_7);
}

int main(void)
{
    RUN_TEST(disables_left_physical_pwm_before_direction_with_preload);
    RUN_TEST(zero_scaled_pwm_forces_brake_direction_and_no_pwm);

    return (test_failures == 0) ? 0 : 1;
}
