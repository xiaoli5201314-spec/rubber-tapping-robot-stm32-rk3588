#ifndef TEST_RC_MSP_MAIN_H
#define TEST_RC_MSP_MAIN_H

#include <stdint.h>

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

typedef struct {
    GPIO_InitTypeDef last_init;
} GPIO_TypeDef;

typedef struct {
    void *Instance;
} TIM_HandleTypeDef;

typedef struct {
    void *Instance;
} UART_HandleTypeDef;

extern GPIO_TypeDef fake_gpioa;
extern GPIO_TypeDef fake_gpiob;
extern GPIO_TypeDef fake_gpiof;
extern uint32_t fake_tim2;
extern uint32_t fake_tim4;
extern uint32_t fake_tim10;
extern uint32_t fake_usart1;

#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define GPIOF (&fake_gpiof)
#define TIM2 (&fake_tim2)
#define TIM4 (&fake_tim4)
#define TIM10 (&fake_tim10)
#define USART1 (&fake_usart1)

#define RC_CH1_Pin ((uint16_t)0x0400U)
#define RC_CH2_Pin ((uint16_t)0x0800U)
#define RC_CH5_Pin ((uint16_t)0x0040U)
#define RC_CH5_GPIO_Port GPIOF
#define MOTOR_LEFT_PWM_Pin ((uint16_t)0x0040U)
#define MOTOR_RIGHT_PWM_Pin ((uint16_t)0x0080U)
#define USART1_TX_Pin ((uint16_t)0x0200U)
#define USART1_RX_Pin ((uint16_t)0x0400U)

#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_PULLUP 1U
#define GPIO_PULLDOWN 2U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define GPIO_AF1_TIM2 1U
#define GPIO_AF2_TIM4 2U
#define GPIO_AF3_TIM10 3U
#define GPIO_AF7_USART1 7U
#define TIM2_IRQn 28
#define TIM1_UP_TIM10_IRQn 25
#define USART1_IRQn 37

#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM4_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM10_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART1_CLK_ENABLE() ((void)0)

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init);
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_TIM_IC_MspInit(TIM_HandleTypeDef *timer);

#endif
