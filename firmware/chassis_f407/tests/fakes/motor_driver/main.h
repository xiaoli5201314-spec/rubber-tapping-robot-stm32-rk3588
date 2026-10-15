#ifndef TEST_MOTOR_DRIVER_MAIN_H
#define TEST_MOTOR_DRIVER_MAIN_H

#include "stm32f4xx_hal.h"

extern GPIO_TypeDef fake_gpiob;
extern GPIO_TypeDef fake_gpiod;
extern GPIO_TypeDef fake_gpioe;

#define GPIOB (&fake_gpiob)
#define GPIOD (&fake_gpiod)
#define GPIOE (&fake_gpioe)

#define GPIO_PIN_5  ((uint16_t)0x0020U)
#define GPIO_PIN_6  ((uint16_t)0x0040U)
#define GPIO_PIN_7  ((uint16_t)0x0080U)

#endif /* TEST_MOTOR_DRIVER_MAIN_H */
