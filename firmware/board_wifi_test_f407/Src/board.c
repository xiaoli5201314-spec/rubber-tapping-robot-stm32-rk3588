#include "board.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

#define RX_SIZE 4096U
#define LED_ERR (1UL << 9)
#define LED_RUN (1UL << 10)

static volatile uint32_t s_ticks;
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint32_t s_rx_errors;
static uint8_t s_rx[RX_SIZE];

void SysTick_Handler(void)
{
    ++s_ticks;
}

void USART2_IRQHandler(void)
{
    const uint32_t status = USART2->SR;
    if ((status & (USART_SR_RXNE | USART_SR_ORE |
                   USART_SR_FE | USART_SR_NE | USART_SR_PE)) != 0U)
    {
        const uint8_t byte = (uint8_t)USART2->DR;
        if ((status & (USART_SR_ORE | USART_SR_FE |
                       USART_SR_NE | USART_SR_PE)) != 0U)
        {
            ++s_rx_errors;
        }
        if ((status & USART_SR_RXNE) != 0U)
        {
            const uint16_t next = (uint16_t)((s_rx_head + 1U) % RX_SIZE);
            if (next != s_rx_tail)
            {
                s_rx[s_rx_head] = byte;
                s_rx_head = next;
            }
            else
            {
                ++s_rx_errors;
            }
        }
    }
}

uint32_t Board_Millis(void)
{
    return s_ticks;
}

void Board_Leds(uint8_t err_on, uint8_t run_on)
{
    const uint32_t on = (err_on ? LED_ERR : 0U) |
                        (run_on ? LED_RUN : 0U);
    GPIOF->BSRR = (on << 16) | ((LED_ERR | LED_RUN) & ~on);
}

static void uart_write(USART_TypeDef *uart, const char *text)
{
    while (*text != '\0')
    {
        while ((uart->SR & USART_SR_TXE) == 0U) {}
        uart->DR = (uint8_t)*text++;
    }
}

void Board_Debug(const char *text)
{
    uart_write(USART1, text);
}

void Board_DebugLine(const char *text)
{
    Board_Debug(text);
    Board_Debug("\r\n");
}

void Board_WifiWrite(const char *text)
{
    uart_write(USART2, text);
}

int Board_WifiRead(void)
{
    int byte;
    if (s_rx_tail == s_rx_head)
    {
        return -1;
    }
    byte = s_rx[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % RX_SIZE);
    return byte;
}

uint32_t Board_WifiRxErrors(void)
{
    return s_rx_errors;
}

void Board_WifiBaud(uint32_t baud)
{
    NVIC_DisableIRQ(USART2_IRQn);
    USART2->CR1 = 0U;
    (void)USART2->SR;
    (void)USART2->DR;
    s_rx_head = 0U;
    s_rx_tail = 0U;
    /* APB1 = HSI / 1 = 16 MHz in this standalone project, oversampling 16. */
    USART2->BRR = (SystemCoreClock + baud / 2U) / baud;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                  USART_CR1_RXNEIE;
    NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
}

void Board_DeviceId(char *out)
{
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    (void)snprintf(out, 25U, "%08lX%08lX%08lX",
                   (unsigned long)uid[0], (unsigned long)uid[1],
                   (unsigned long)uid[2]);
}

void Board_Init(void)
{
    SystemCoreClockUpdate();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOFEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->AHB1ENR;
    (void)RCC->APB1ENR;
    __DSB();

    GPIOF->BSRR = LED_ERR | LED_RUN;
    GPIOF->OTYPER &= ~(LED_ERR | LED_RUN);
    GPIOF->OSPEEDR &= ~((3UL << 18) | (3UL << 20));
    GPIOF->PUPDR &= ~((3UL << 18) | (3UL << 20));
    GPIOF->MODER = (GPIOF->MODER & ~((3UL << 18) | (3UL << 20))) |
                    (1UL << 18) | (1UL << 20);

    /* PA2 = USART2_TX -> module RXD0; PA3 = USART2_RX <- module TXD0. */
    /* PA9/PA10 = USART1_TX/RX on the separate debug connector. */
    GPIOA->MODER = (GPIOA->MODER &
                   ~((3UL << 4) | (3UL << 6) |
                     (3UL << 18) | (3UL << 20))) |
                   (2UL << 4) | (2UL << 6) |
                   (2UL << 18) | (2UL << 20);
    GPIOA->OTYPER &= ~((1UL << 2) | (1UL << 3) |
                      (1UL << 9) | (1UL << 10));
    GPIOA->OSPEEDR |= (2UL << 4) | (2UL << 6) |
                       (2UL << 18) | (2UL << 20);
    GPIOA->PUPDR = (GPIOA->PUPDR &
                   ~((3UL << 4) | (3UL << 6) |
                     (3UL << 18) | (3UL << 20))) |
                   (1UL << 6) | (1UL << 20);
    GPIOA->AFR[0] = (GPIOA->AFR[0] &
                    ~((15UL << 8) | (15UL << 12))) |
                    (7UL << 8) | (7UL << 12);
    GPIOA->AFR[1] = (GPIOA->AFR[1] &
                    ~((15UL << 4) | (15UL << 8))) |
                    (7UL << 4) | (7UL << 8);

    USART1->BRR = (SystemCoreClock + 57600U) / 115200U;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    NVIC_SetPriority(USART2_IRQn, 1U);
    Board_WifiBaud(115200U);
    (void)SysTick_Config(SystemCoreClock / 1000U);
    __enable_irq();
}
