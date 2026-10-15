#ifndef __TIM_H
#define __TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim10;

void MX_TIM2_Init(void);
void MX_TIM4_Init(void);
void MX_TIM10_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIM_H */
