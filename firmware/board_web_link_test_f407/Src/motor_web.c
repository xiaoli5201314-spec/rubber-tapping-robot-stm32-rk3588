#include "motor_web.h"
#include "board.h"
#include "board_config.h"
#include "stm32f4xx.h"

/* HSI 16 MHz, APB1 /1: 16 MHz / 1600 = 10 kHz.
   CCR preload stays disabled so emergency zero takes effect immediately. */
#define MOTOR_PWM_COUNTS 1600U

static int16_t s_left;
static int16_t s_right;

void MotorWeb_Stop(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN |
                    RCC_AHB1ENR_GPIOEEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    (void)RCC->APB1ENR;
    TIM4->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC2E);
    TIM4->CCR1 = 0U;
    TIM4->CCR2 = 0U;
    GPIOD->BSRR = (1UL << 22) | (1UL << 23);
    GPIOE->BSRR = (1UL << 21) | (1UL << 22);
    s_left = 0;
    s_right = 0;
}

uint8_t MotorWeb_Init(void)
{
    uint32_t started;
    MotorWeb_Stop();
    TIM4->CR1 = 0U;
    TIM4->DIER = 0U;
    TIM4->CCER = 0U;
    TIM4->PSC = 0U;
    TIM4->ARR = MOTOR_PWM_COUNTS - 1U;
    TIM4->CCMR1 = (6UL << 4) | (6UL << 12); /* PWM1, active-high */
    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0U;
    TIM4->CR1 = TIM_CR1_CEN;
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~((15UL << 24) | (15UL << 28))) |
                    (2UL << 24) | (2UL << 28);
    GPIOB->MODER = (GPIOB->MODER & ~((3UL << 12) | (3UL << 14))) |
                   (2UL << 12) | (2UL << 14);
    /* Independent LSI watchdog: nominal 500 ms. If control interrupts stop,
       reset returns to locked state; foreground network activity cannot feed it. */
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = 4U;    /* /64 */
    IWDG->RLR = 249U;
    started = Board_Millis();
    while (IWDG->SR != 0U)
    {
        if ((uint32_t)(Board_Millis() - started) > 100U)
        {
            MotorWeb_Stop();
            return 0U;
        }
    }
    MotorWeb_FeedWatchdog();
    return 1U;
}

static int16_t clamp(int16_t value)
{
    if (value > (int16_t)MOTOR_MAX_OUTPUT_PERMILLE)
        return (int16_t)MOTOR_MAX_OUTPUT_PERMILLE;
    if (value < -(int16_t)MOTOR_MAX_OUTPUT_PERMILLE)
        return -(int16_t)MOTOR_MAX_OUTPUT_PERMILLE;
    return value;
}

static void apply_side(volatile uint32_t *ccr, uint32_t enable,
                       GPIO_TypeDef *gpio, uint32_t pin, int16_t value,
                       int16_t *previous)
{
    uint32_t magnitude = value < 0 ? (uint32_t)(-(int32_t)value) : (uint32_t)value;
    uint32_t compare = magnitude * MOTOR_PWM_COUNTS / 1000U;
    uint8_t direction_changed = (uint8_t)(
        (value > 0 && *previous <= 0) || (value < 0 && *previous >= 0));
    if (value == 0 || direction_changed)
    {
        /* Disable this channel and write active CCR=0 before touching direction.
           The other motor's channel and the shared counter remain untouched. */
        TIM4->CCER &= ~enable;
        *ccr = 0U;
        if (value > 0)
            gpio->BSRR = (1UL << pin) | (1UL << (pin + 17U));
        else if (value < 0)
            gpio->BSRR = (1UL << (pin + 1U)) | (1UL << (pin + 16U));
        else
            gpio->BSRR = 3UL << (pin + 16U);
    }
    *ccr = compare;
    if (compare != 0U) TIM4->CCER |= enable;
    *previous = value;
}

void MotorWeb_Apply(int16_t left, int16_t right)
{
    left = clamp(left);
    right = clamp(right);
    if (WEB_MOTOR_LEFT_INVERT) left = (int16_t)-left;
    if (WEB_MOTOR_RIGHT_INVERT) right = (int16_t)-right;
    apply_side(&TIM4->CCR1, TIM_CCER_CC1E, GPIOD, 6U, left, &s_left);
    apply_side(&TIM4->CCR2, TIM_CCER_CC2E, GPIOE, 5U, right, &s_right);
}

void MotorWeb_FeedWatchdog(void)
{
    IWDG->KR = 0xAAAAU;
}

static void fatal_stop(void)
{
    __disable_irq();
    MotorWeb_Stop();
    while (1) { __WFI(); } /* Watchdog resets; never restart motion here. */
}

void HardFault_Handler(void) { fatal_stop(); }
void MemManage_Handler(void) { fatal_stop(); }
void BusFault_Handler(void) { fatal_stop(); }
void UsageFault_Handler(void) { fatal_stop(); }
void NMI_Handler(void) { fatal_stop(); }
