#include "main.h"
#include "test_support.h"

GPIO_TypeDef fake_gpioa;
GPIO_TypeDef fake_gpiob;
GPIO_TypeDef fake_gpiof;
uint32_t fake_tim2;
uint32_t fake_tim4;
uint32_t fake_tim10;
uint32_t fake_usart1;

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init)
{
    port->last_init = *init;
}

void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub)
{
    (void)irq;
    (void)preempt;
    (void)sub;
}

void HAL_NVIC_EnableIRQ(int irq)
{
    (void)irq;
}

/* Catch enabling internal pulls on FT inputs that may receive 5 V PWM. */
TEST(rc_ch1_ch2_use_timer_input_without_internal_pulls)
{
    TIM_HandleTypeDef timer = {TIM2};

    HAL_TIM_IC_MspInit(&timer);

    ASSERT_EQ_U32(0x0C00U, fake_gpiob.last_init.Pin);
    ASSERT_EQ_U32(GPIO_MODE_AF_PP, fake_gpiob.last_init.Mode);
    ASSERT_EQ_U32(GPIO_AF1_TIM2, fake_gpiob.last_init.Alternate);
    ASSERT_EQ_U32(GPIO_NOPULL, fake_gpiob.last_init.Pull);
}

TEST(rc_ch5_uses_digital_timer_input_without_internal_pulls)
{
    TIM_HandleTypeDef timer = {TIM10};

    HAL_TIM_IC_MspInit(&timer);

    ASSERT_EQ_U32(0x0040U, fake_gpiof.last_init.Pin);
    ASSERT_EQ_U32(GPIO_MODE_AF_PP, fake_gpiof.last_init.Mode);
    ASSERT_EQ_U32(GPIO_AF3_TIM10, fake_gpiof.last_init.Alternate);
    ASSERT_EQ_U32(GPIO_NOPULL, fake_gpiof.last_init.Pull);
}

int main(void)
{
    RUN_TEST(rc_ch1_ch2_use_timer_input_without_internal_pulls);
    RUN_TEST(rc_ch5_uses_digital_timer_input_without_internal_pulls);
    return test_failures;
}
