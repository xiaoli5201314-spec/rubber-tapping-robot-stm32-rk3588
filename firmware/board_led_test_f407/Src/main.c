/*
 * Standalone LED test for the user's STM32F407ZGT6 board.
 * ERRLED / LED3: PF9; RUNLED / LED4: PF10. Both are active low.
 * Only the two LED GPIO pins and SysTick are configured.
 */
#include "stm32f4xx.h"

#define ERRLED_MASK      (1UL << 9)
#define RUNLED_MASK      (1UL << 10)
#define LED_ALL_MASK     (ERRLED_MASK | RUNLED_MASK)
#define LED_MODE_MASK    ((3UL << 18) | (3UL << 20))
#define LED_OUTPUT_MODE  ((1UL << 18) | (1UL << 20))

static volatile uint32_t s_milliseconds = 0U;

void SysTick_Handler(void)
{
    ++s_milliseconds;
}

static void delay_ms(uint32_t duration)
{
    const uint32_t start = s_milliseconds;

    /* Unsigned subtraction also works when the millisecond counter wraps. */
    while ((uint32_t)(s_milliseconds - start) < duration)
    {
        __NOP();
    }
}

static void leds_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;
    __DSB();

    /* Preload HIGH (= off) before switching the pins to output mode. */
    GPIOF->BSRR = LED_ALL_MASK;
    GPIOF->OTYPER &= ~LED_ALL_MASK;
    GPIOF->OSPEEDR &= ~LED_MODE_MASK;
    GPIOF->PUPDR &= ~LED_MODE_MASK;
    GPIOF->MODER = (GPIOF->MODER & ~LED_MODE_MASK) | LED_OUTPUT_MODE;
}

static void leds_set(uint32_t on_mask)
{
    on_mask &= LED_ALL_MASK;

    /* BSRR upper half drives LOW (= on); lower half drives HIGH (= off). */
    GPIOF->BSRR = (on_mask << 16) | (LED_ALL_MASK & ~on_mask);
}

int main(void)
{
    uint32_t flash;

    leds_init();
    SystemCoreClockUpdate();

    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        /* A steady pair of lights identifies a failed tick configuration. */
        leds_set(LED_ALL_MASK);
        while (1)
        {
            __NOP();
        }
    }
    __enable_irq();

    while (1)
    {
        /* Test cycle: 1 s off, 1 s ERRLED, 1 s RUNLED, 3 pair flashes. */
        /* ERRLED is intentionally lit here; it does not indicate a fault. */
        leds_set(0U);
        delay_ms(1000U);

        leds_set(ERRLED_MASK);
        delay_ms(1000U);

        leds_set(RUNLED_MASK);
        delay_ms(1000U);

        leds_set(0U);
        delay_ms(250U);

        for (flash = 0U; flash < 3U; ++flash)
        {
            leds_set(LED_ALL_MASK);
            delay_ms(250U);
            leds_set(0U);
            delay_ms(250U);
        }
    }
}
