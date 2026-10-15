/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED0_Pin GPIO_PIN_9
#define LED0_GPIO_Port GPIOF
#define LED1_Pin GPIO_PIN_10
#define LED1_GPIO_Port GPIOF
#define RC_CH1_Pin GPIO_PIN_10
#define RC_CH1_GPIO_Port GPIOB
#define RC_CH2_Pin GPIO_PIN_11
#define RC_CH2_GPIO_Port GPIOB
#define RC_CH5_Pin GPIO_PIN_6
#define RC_CH5_GPIO_Port GPIOF
#define MOTOR_LEFT_PWM_Pin GPIO_PIN_6
#define MOTOR_LEFT_PWM_GPIO_Port GPIOB
#define MOTOR_RIGHT_PWM_Pin GPIO_PIN_7
#define MOTOR_RIGHT_PWM_GPIO_Port GPIOB
#define MOTOR_LEFT_IN1_Pin GPIO_PIN_6
#define MOTOR_LEFT_IN1_GPIO_Port GPIOD
#define MOTOR_LEFT_IN2_Pin GPIO_PIN_7
#define MOTOR_LEFT_IN2_GPIO_Port GPIOD
#define MOTOR_RIGHT_IN1_Pin GPIO_PIN_5
#define MOTOR_RIGHT_IN1_GPIO_Port GPIOE
#define MOTOR_RIGHT_IN2_Pin GPIO_PIN_6
#define MOTOR_RIGHT_IN2_GPIO_Port GPIOE
#define USART1_TX_Pin GPIO_PIN_9
#define USART1_TX_GPIO_Port GPIOA
#define USART1_RX_Pin GPIO_PIN_10
#define USART1_RX_GPIO_Port GPIOA
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
