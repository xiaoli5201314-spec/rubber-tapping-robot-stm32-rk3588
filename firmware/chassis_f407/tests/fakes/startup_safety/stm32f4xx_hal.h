#ifndef TEST_STARTUP_SAFETY_STM32F4XX_HAL_H
#define TEST_STARTUP_SAFETY_STM32F4XX_HAL_H

#include <stdint.h>

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

typedef struct {
    uint32_t ODR;
    uint32_t output_mask;
} GPIO_TypeDef;

typedef struct {
    uint32_t CCER;
    uint32_t CCR1;
    uint32_t CCR2;
} TIM_TypeDef;

extern GPIO_TypeDef fake_gpiob;
extern GPIO_TypeDef fake_gpiod;
extern GPIO_TypeDef fake_gpioe;
extern TIM_TypeDef fake_tim4;

#define GPIOB (&fake_gpiob)
#define GPIOD (&fake_gpiod)
#define GPIOE (&fake_gpioe)
#define TIM4  (&fake_tim4)

#define GPIO_MODE_OUTPUT_PP    1U
#define GPIO_NOPULL            0U
#define GPIO_SPEED_FREQ_LOW    0U
#define TIM_CCER_CC1E          0x00000001U
#define TIM_CCER_CC2E          0x00000010U

void FakeHal_EnableGpiobClock(void);
void FakeHal_EnableGpiodClock(void);
void FakeHal_EnableGpioeClock(void);
void FakeHal_EnableTim4Clock(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init);
void __disable_irq(void);

#define __HAL_RCC_GPIOB_CLK_ENABLE() FakeHal_EnableGpiobClock()
#define __HAL_RCC_GPIOD_CLK_ENABLE() FakeHal_EnableGpiodClock()
#define __HAL_RCC_GPIOE_CLK_ENABLE() FakeHal_EnableGpioeClock()
#define __HAL_RCC_TIM4_CLK_ENABLE()  FakeHal_EnableTim4Clock()

#endif /* TEST_STARTUP_SAFETY_STM32F4XX_HAL_H */
