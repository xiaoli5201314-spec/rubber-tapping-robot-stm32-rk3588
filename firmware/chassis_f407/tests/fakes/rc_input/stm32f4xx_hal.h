#ifndef TEST_RC_INPUT_STM32F4XX_HAL_H
#define TEST_RC_INPUT_STM32F4XX_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

typedef enum {
    HAL_OK = 0,
    HAL_ERROR
} HAL_StatusTypeDef;

typedef struct {
    volatile uint32_t SR;
    volatile uint32_t CCR1;
    volatile uint32_t CCR3;
    volatile uint32_t CCR4;
} TIM_TypeDef;

typedef struct {
    volatile uint32_t IDR;
} GPIO_TypeDef;

typedef struct {
    TIM_TypeDef *Instance;
    uint32_t Channel;
} TIM_HandleTypeDef;

#define RESET                         0U
#define TIM_CHANNEL_1                 0x00000000U
#define TIM_CHANNEL_3                 0x00000008U
#define TIM_CHANNEL_4                 0x0000000CU
#define HAL_TIM_ACTIVE_CHANNEL_1      0x01U
#define HAL_TIM_ACTIVE_CHANNEL_3      0x04U
#define HAL_TIM_ACTIVE_CHANNEL_4      0x08U
#define TIM_FLAG_CC1                  0x00000002U
#define TIM_FLAG_CC3                  0x00000008U
#define TIM_FLAG_CC4                  0x00000010U
#define TIM_FLAG_CC1OF                0x00000200U
#define TIM_FLAG_CC3OF                0x00000800U
#define TIM_FLAG_CC4OF                0x00001000U

uint32_t FakeHal_ReadCapturedValue(TIM_HandleTypeDef *timer, uint32_t channel);
GPIO_PinState FakeHal_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void FakeHal_ClearTimFlags(TIM_HandleTypeDef *timer, uint32_t flags);
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel);
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer);
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);

#define HAL_TIM_ReadCapturedValue(__HANDLE__, __CHANNEL__) \
    FakeHal_ReadCapturedValue((__HANDLE__), (__CHANNEL__))
#define HAL_GPIO_ReadPin(__PORT__, __PIN__) FakeHal_ReadPin((__PORT__), (__PIN__))
#define __HAL_TIM_GET_FLAG(__HANDLE__, __FLAG__) \
    (((__HANDLE__)->Instance->SR & (__FLAG__)) == (__FLAG__))
#define __HAL_TIM_CLEAR_FLAG(__HANDLE__, __FLAG__) \
    FakeHal_ClearTimFlags((__HANDLE__), (__FLAG__))

#endif /* TEST_RC_INPUT_STM32F4XX_HAL_H */
