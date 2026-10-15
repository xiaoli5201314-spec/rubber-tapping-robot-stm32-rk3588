#ifndef TEST_RC_INPUT_MAIN_H
#define TEST_RC_INPUT_MAIN_H

#include "stm32f4xx_hal.h"

extern GPIO_TypeDef fake_gpiob;
extern GPIO_TypeDef fake_gpiof;

#define GPIOB (&fake_gpiob)
#define GPIOF (&fake_gpiof)

#define GPIO_PIN_6   ((uint16_t)0x0040U)
#define GPIO_PIN_10  ((uint16_t)0x0400U)
#define GPIO_PIN_11  ((uint16_t)0x0800U)

#endif /* TEST_RC_INPUT_MAIN_H */
