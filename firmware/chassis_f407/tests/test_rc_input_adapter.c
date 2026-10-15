#include <string.h>

#include "main.h"
#include "rc_input.h"
#include "test_support.h"
#include "tim.h"

GPIO_TypeDef fake_gpiob;
GPIO_TypeDef fake_gpiof;
static TIM_TypeDef fake_tim2;
static TIM_TypeDef fake_tim10;
TIM_HandleTypeDef htim2 = { &fake_tim2, HAL_TIM_ACTIVE_CHANNEL_3 };
TIM_HandleTypeDef htim10 = { &fake_tim10, HAL_TIM_ACTIVE_CHANNEL_1 };

static uint32_t fake_tick;
static uint32_t inject_flag_on_gpio_read;

uint32_t FakeHal_ReadCapturedValue(TIM_HandleTypeDef *timer, uint32_t channel)
{
    if (channel == TIM_CHANNEL_3) {
        return timer->Instance->CCR3;
    }
    if (channel == TIM_CHANNEL_4) {
        return timer->Instance->CCR4;
    }
    return timer->Instance->CCR1;
}

GPIO_PinState FakeHal_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_PinState state = ((port->IDR & pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    fake_tim2.SR |= inject_flag_on_gpio_read;
    inject_flag_on_gpio_read = 0U;
    return state;
}

void FakeHal_ClearTimFlags(TIM_HandleTypeDef *timer, uint32_t flags)
{
    timer->Instance->SR &= ~flags;
}

HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    (void)channel;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

uint32_t __get_PRIMASK(void)
{
    return 0U;
}

void __disable_irq(void)
{
}

void __enable_irq(void)
{
}

static void SendCh1Edge(uint32_t captured, GPIO_PinState level, uint32_t now_ms)
{
    fake_tick = now_ms;
    fake_tim2.CCR3 = captured;
    if (level == GPIO_PIN_SET) {
        fake_gpiob.IDR |= GPIO_PIN_10;
    } else {
        fake_gpiob.IDR &= ~(uint32_t)GPIO_PIN_10;
    }
    htim2.Channel = HAL_TIM_ACTIVE_CHANNEL_3;
    HAL_TIM_IC_CaptureCallback(&htim2);
}

static void MakeCh1Valid(void)
{
    SendCh1Edge(10000U, GPIO_PIN_SET, 1U);
    SendCh1Edge(11500U, GPIO_PIN_RESET, 2U);
    SendCh1Edge(30000U, GPIO_PIN_SET, 20U);
    SendCh1Edge(31500U, GPIO_PIN_RESET, 21U);
}

static void ResetFakeHardware(void)
{
    (void)memset(&fake_gpiob, 0, sizeof(fake_gpiob));
    (void)memset(&fake_gpiof, 0, sizeof(fake_gpiof));
    (void)memset(&fake_tim2, 0, sizeof(fake_tim2));
    (void)memset(&fake_tim10, 0, sizeof(fake_tim10));
    fake_tick = 0U;
    inject_flag_on_gpio_read = 0U;
    htim2.Channel = HAL_TIM_ACTIVE_CHANNEL_3;
    htim10.Channel = HAL_TIM_ACTIVE_CHANNEL_1;
    RcInput_Init();
    MakeCh1Valid();
}

static void AssertCh1SampleIsInvalidated(uint32_t new_flag)
{
    RcInputSnapshot snapshot;

    inject_flag_on_gpio_read = new_flag;
    SendCh1Edge(50000U, GPIO_PIN_SET, 40U);
    RcInput_GetSnapshot(&snapshot);

    ASSERT_EQ_U32(0U, snapshot.ch1_valid);
    ASSERT_EQ_U32(0U, snapshot.ch1_fresh);
    ASSERT_EQ_U32(0U, fake_tim2.SR & (TIM_FLAG_CC3 | TIM_FLAG_CC3OF));
}

TEST(new_cc3if_during_gpio_sample_invalidates_ch1_without_decoding)
{
    ResetFakeHardware();
    AssertCh1SampleIsInvalidated(TIM_FLAG_CC3);
}

TEST(new_cc3of_during_gpio_sample_invalidates_ch1_without_decoding)
{
    ResetFakeHardware();
    AssertCh1SampleIsInvalidated(TIM_FLAG_CC3OF);
}

int main(void)
{
    RUN_TEST(new_cc3if_during_gpio_sample_invalidates_ch1_without_decoding);
    RUN_TEST(new_cc3of_during_gpio_sample_invalidates_ch1_without_decoding);

    return (test_failures == 0) ? 0 : 1;
}
