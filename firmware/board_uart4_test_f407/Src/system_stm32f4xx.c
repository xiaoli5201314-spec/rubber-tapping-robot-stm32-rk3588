/*
 * Minimal clock setup for this UART4 communication test.
 * SYSCLK/HCLK/PCLK1/PCLK2 use the internal 16 MHz HSI with no PLL.
 * External crystal, LCD and external memory are not initialized.
 */
#include "stm32f4xx.h"

uint32_t SystemCoreClock = 16000000U;

const uint8_t AHBPrescTable[16] =
{
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U
};

const uint8_t APBPrescTable[8] =
{
    0U, 0U, 0U, 0U, 1U, 2U, 3U, 4U
};

void SystemInit(void)
{
#if (__FPU_PRESENT == 1U) && (__FPU_USED == 1U)
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
#endif

    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U)
    {
        /* HSI is also the reset clock source. */
    }

    RCC->CFGR &= ~RCC_CFGR_SW;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
    {
    }

    /* Switch first, then disable the unused external/PLL clock sources. */
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON |
                 RCC_CR_PLLON | RCC_CR_PLLI2SON);
    RCC->CFGR = 0U;
    RCC->CIR = 0U;

    SCB->VTOR = FLASH_BASE;
    __DSB();
    __ISB();
}

void SystemCoreClockUpdate(void)
{
    /* This project always selects HSI. This is not a generic PLL decoder. */
    const uint32_t ahb_index =
        (RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos;

    SystemCoreClock = 16000000U >> AHBPrescTable[ahb_index];
}
