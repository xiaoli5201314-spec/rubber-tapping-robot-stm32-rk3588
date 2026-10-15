#include "uart4_io.h"
#include "stm32f4xx.h"

#define RX_CAPACITY 1024U
#define ERRLED (1UL << 9)
#define RUNLED (1UL << 10)

static volatile uint32_t s_milliseconds;
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_rx[RX_CAPACITY];
static volatile Uart4Stats s_stats;

void SysTick_Handler(void)
{
    ++s_milliseconds;
}

void UART4_IRQHandler(void)
{
    const uint32_t status = UART4->SR;
    const uint32_t error_mask =
        USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE;
    if ((status & (USART_SR_RXNE | error_mask)) != 0U)
    {
        /* Reading SR followed by DR clears RXNE and the UART error flags. */
        const uint8_t byte = (uint8_t)UART4->DR;
        if ((status & error_mask) != 0U)
        {
            ++s_stats.uart_errors;
        }
        else if ((status & USART_SR_RXNE) != 0U)
        {
            const uint16_t next =
                (uint16_t)((s_rx_head + 1U) % RX_CAPACITY);
            ++s_stats.rx_bytes;
            if (next != s_rx_tail)
            {
                s_rx[s_rx_head] = byte;
                s_rx_head = next;
            }
            else
            {
                ++s_stats.buffer_overflows;
            }
        }
    }
}

uint32_t Uart4_Millis(void)
{
    return s_milliseconds;
}

int Uart4_Read(void)
{
    int byte;
    if (s_rx_tail == s_rx_head) return -1;
    byte = s_rx[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % RX_CAPACITY);
    return byte;
}

int Uart4_Write(const char *text)
{
    while (*text != '\0')
    {
        const uint32_t start = Uart4_Millis();
        while ((UART4->SR & USART_SR_TXE) == 0U)
        {
            if ((uint32_t)(Uart4_Millis() - start) >= 50U)
            {
                ++s_stats.tx_timeouts;
                return 0;
            }
        }
        UART4->DR = (uint8_t)*text++;
        ++s_stats.tx_bytes;
    }
    return 1;
}

void Uart4_GetStats(Uart4Stats *stats)
{
    /* A short IRQ guard gives counters from the same instant. */
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *stats = s_stats;
    __set_PRIMASK(mask);
}

void Uart4_UpdateLeds(uint8_t application_error)
{
    const uint8_t error = (uint8_t)(application_error ||
        s_stats.uart_errors || s_stats.buffer_overflows || s_stats.tx_timeouts);
    const uint32_t on = (error ? ERRLED : 0U) |
        (((Uart4_Millis() / 500U) & 1U) ? RUNLED : 0U);
    GPIOF->BSRR = (on << 16) | ((ERRLED | RUNLED) & ~on);
}

void Uart4_Init(void)
{
    SystemCoreClockUpdate();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOFEN;
    RCC->APB1ENR |= RCC_APB1ENR_UART4EN;
    (void)RCC->AHB1ENR;
    (void)RCC->APB1ENR;
    __DSB();
    RCC->APB1RSTR |= RCC_APB1RSTR_UART4RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_UART4RST;

    /* Actual PCB: PC10/TX -> CH340N RXD, PC11/RX <- CH340N TXD. */
    GPIOC->OTYPER &= ~((1UL << 10) | (1UL << 11));
    GPIOC->OSPEEDR =
        (GPIOC->OSPEEDR & ~((3UL << 20) | (3UL << 22))) |
        (2UL << 20) | (2UL << 22);
    GPIOC->PUPDR =
        (GPIOC->PUPDR & ~((3UL << 20) | (3UL << 22))) | (1UL << 22);
    GPIOC->AFR[1] =
        (GPIOC->AFR[1] & ~((15UL << 8) | (15UL << 12))) |
        (8UL << 8) | (8UL << 12);
    GPIOC->MODER =
        (GPIOC->MODER & ~((3UL << 20) | (3UL << 22))) |
        (2UL << 20) | (2UL << 22);

    GPIOF->BSRR = ERRLED | RUNLED;
    GPIOF->OTYPER &= ~(ERRLED | RUNLED);
    GPIOF->OSPEEDR &= ~((3UL << 18) | (3UL << 20));
    GPIOF->PUPDR &= ~((3UL << 18) | (3UL << 20));
    GPIOF->MODER =
        (GPIOF->MODER & ~((3UL << 18) | (3UL << 20))) |
        (1UL << 18) | (1UL << 20);

    /* APB1=HSI=16 MHz. 115200 baud, 8 data bits, no parity, 1 stop bit. */
    UART4->CR1 = 0U;
    UART4->BRR = (SystemCoreClock + 57600U) / 115200U;
    UART4->CR2 = 0U;
    UART4->CR3 = 0U; /* No CTS/RTS hardware flow control. */
    (void)UART4->SR;
    (void)UART4->DR;
    UART4->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                 USART_CR1_RXNEIE;
    NVIC_SetPriority(UART4_IRQn, 1U);
    NVIC_ClearPendingIRQ(UART4_IRQn);
    NVIC_EnableIRQ(UART4_IRQn);
    (void)SysTick_Config(SystemCoreClock / 1000U);
    __enable_irq();
}
