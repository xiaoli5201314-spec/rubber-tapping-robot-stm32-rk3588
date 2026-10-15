#ifndef TEST_MOTOR_DRIVER_STM32F4XX_HAL_H
#define TEST_MOTOR_DRIVER_STM32F4XX_HAL_H

#include <stdint.h>

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

typedef struct {
    volatile uint32_t CCMR1;
    volatile uint32_t CCER;
} TIM_TypeDef;

typedef struct {
    volatile uint32_t ODR;
    uint32_t output_pins;
    uint32_t clock_enabled;
} GPIO_TypeDef;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

typedef struct {
    TIM_TypeDef *Instance;
    uint32_t Channel;
} TIM_HandleTypeDef;

#define TIM_CHANNEL_1       0x00000000U
#define TIM_CHANNEL_2       0x00000004U
#define TIM_CCx_DISABLE     0U
#define TIM_CCx_ENABLE      1U
#define TIM_CCER_CC1E       0x00000001U
#define TIM_CCER_CC2E       0x00000010U
#define TIM_CCMR1_OC1PE     0x00000008U
#define TIM_CCMR1_OC2PE     0x00000800U

#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define __HAL_RCC_GPIOB_CLK_ENABLE() (fake_gpiob.clock_enabled = 1U)
#define __HAL_RCC_GPIOD_CLK_ENABLE() (fake_gpiod.clock_enabled = 1U)
#define __HAL_RCC_GPIOE_CLK_ENABLE() (fake_gpioe.clock_enabled = 1U)

void FakeHal_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t compare);
void TIM_CCxChannelCmd(TIM_TypeDef *timer, uint32_t channel, uint32_t state);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config);

#define __HAL_TIM_SET_COMPARE(__HANDLE__, __CHANNEL__, __COMPARE__) \
    FakeHal_SetCompare((__HANDLE__), (__CHANNEL__), (__COMPARE__))
#define __HAL_TIM_DISABLE_OCxPRELOAD(__HANDLE__, __CHANNEL__) \
    (((__CHANNEL__) == TIM_CHANNEL_1) ? ((__HANDLE__)->Instance->CCMR1 &= ~TIM_CCMR1_OC1PE) : \
     ((__HANDLE__)->Instance->CCMR1 &= ~TIM_CCMR1_OC2PE))
#define __HAL_TIM_ENABLE_OCxPRELOAD(__HANDLE__, __CHANNEL__) \
    (((__CHANNEL__) == TIM_CHANNEL_1) ? ((__HANDLE__)->Instance->CCMR1 |= TIM_CCMR1_OC1PE) : \
     ((__HANDLE__)->Instance->CCMR1 |= TIM_CCMR1_OC2PE))

#endif /* TEST_MOTOR_DRIVER_STM32F4XX_HAL_H */
