/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include "tim.h"
#include "usart.h"

#include <string.h>

#include "board_config.h"
#include "chassis_math.h"
#include "diagnostics.h"
#include "motor_driver.h"
#include "motor_profile.h"
#include "rc_input.h"
#include "safety.h"
#include "startup_safety.h"
#include "lcd_display.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

static SafetyStateMachine safety_machine;
static MotorProfile left_profile;
static MotorProfile right_profile;
static RcInputSnapshot latest_snapshot;
static int16_t latest_left_target;
static int16_t latest_right_target;
static MotorOutput latest_left_output = {MOTOR_DIR_BRAKE, 0U};
static MotorOutput latest_right_output = {MOTOR_DIR_BRAKE, 0U};

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void Control_ResetProfiles(void)
{
  (void)memset(&left_profile, 0, sizeof(left_profile));
  (void)memset(&right_profile, 0, sizeof(right_profile));
}

static int16_t Control_ScaleTarget(int16_t command, uint8_t invert)
{
  int32_t target = ((int32_t)command * (int32_t)MOTOR_MAX_OUTPUT_PERMILLE) / 1000;

  if (target > (int32_t)MOTOR_MAX_OUTPUT_PERMILLE) {
    target = (int32_t)MOTOR_MAX_OUTPUT_PERMILLE;
  } else if (target < -(int32_t)MOTOR_MAX_OUTPUT_PERMILLE) {
    target = -(int32_t)MOTOR_MAX_OUTPUT_PERMILLE;
  }
  if (invert != 0U) {
    target = -target;
  }
  return (int16_t)target;
}

static void Control_SetLed(SafetyState state)
{
  GPIO_PinState red = GPIO_PIN_RESET;
  GPIO_PinState green = GPIO_PIN_SET;

  /* PF9/LED0 is red and PF10/LED1 is green; both are active-low. */
  if (state == SAFETY_ARMED) {
    red = GPIO_PIN_SET;
    green = GPIO_PIN_RESET;
  }
  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, red);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, green);
}

static void Control_Task(uint32_t now_ms)
{
  RcInputSnapshot snapshot;
  SafetyInputs inputs;
  ChassisCommand command;

  RcInput_GetSnapshot(&snapshot);
  inputs.ch1_fresh = (uint8_t)((snapshot.ch1_valid != 0U) &&
                               (snapshot.ch1_fresh != 0U));
  inputs.ch2_fresh = (uint8_t)((snapshot.ch2_valid != 0U) &&
                               (snapshot.ch2_fresh != 0U));
  inputs.ch5_fresh = (uint8_t)((snapshot.ch5_valid != 0U) &&
                               (snapshot.ch5_fresh != 0U));
  inputs.ch1_us = snapshot.ch1_us;
  inputs.ch2_us = snapshot.ch2_us;
  inputs.ch5_us = snapshot.ch5_us;
  Safety_Update(&safety_machine, &inputs, now_ms);

  latest_snapshot = snapshot;
  if (Safety_IsDriveAllowed(&safety_machine) == 0U) {
    MotorDriver_EmergencyBrake();
    Control_ResetProfiles();
    latest_left_target = 0;
    latest_right_target = 0;
    latest_left_output.direction = MOTOR_DIR_BRAKE;
    latest_left_output.pwm_permille = 0U;
    latest_right_output.direction = MOTOR_DIR_BRAKE;
    latest_right_output.pwm_permille = 0U;
    Control_SetLed(safety_machine.state);
    return;
  }

  command = ChassisMath_Mix(
      ChassisMath_PulseToCommand(snapshot.ch2_us, RC_CENTER_US,
                                 RC_DEADBAND_US, RC_THROTTLE_INVERT),
      ChassisMath_PulseToCommand(snapshot.ch1_us, RC_CENTER_US,
                                 RC_DEADBAND_US, RC_STEERING_INVERT));
  latest_left_target = Control_ScaleTarget(command.left, MOTOR_LEFT_INVERT);
  latest_right_target = Control_ScaleTarget(command.right, MOTOR_RIGHT_INVERT);
  latest_left_output = MotorProfile_Update(&left_profile, latest_left_target,
                                           now_ms, 0U);
  latest_right_output = MotorProfile_Update(&right_profile, latest_right_target,
                                            now_ms, 0U);
  MotorDriver_ApplyLeft(latest_left_output);
  MotorDriver_ApplyRight(latest_right_output);
  Control_SetLed(safety_machine.state);
}

static void Diagnostics_Task(uint32_t now_ms)
{
  DiagnosticsFrame frame;

  frame.time_ms = now_ms;
  frame.rc = latest_snapshot;
  frame.safety_state = safety_machine.state;
  frame.left_target_permille = latest_left_target;
  frame.right_target_permille = latest_right_target;
  frame.left_output = latest_left_output;
  frame.right_output = latest_right_output;
  (void)Diagnostics_TryTransmit(&frame);
  LCD_Display_Update(&frame);
}

static void Error_FatalLoop(void)
{
  while (1)
  {
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  uint32_t last_control_ms;
  uint32_t last_diagnostics_ms;

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
#if !RC_BENCH_TEST
  MX_TIM4_Init();
#endif
  MX_TIM10_Init();

  MotorDriver_Init();
  MotorDriver_EmergencyBrake();
#if !RC_BENCH_TEST
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0U);
  if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1) != HAL_OK) {
    MotorDriver_EmergencyBrake();
    Error_Handler();
  }
  if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2) != HAL_OK) {
    MotorDriver_EmergencyBrake();
    Error_Handler();
  }
#endif

  LCD_Display_Init();

  RcInput_Init();
  Safety_Init(&safety_machine);
  Control_ResetProfiles();
  if (RcInput_Start() != HAL_OK) {
    MotorDriver_EmergencyBrake();
    Error_Handler();
  }

  last_control_ms = HAL_GetTick();
  Control_Task(last_control_ms);
  last_diagnostics_ms = last_control_ms;

  while (1)
  {
    uint32_t now_ms = HAL_GetTick();

    if ((uint32_t)(now_ms - last_control_ms) >= 10U) {
      last_control_ms = now_ms;
      Control_Task(now_ms);
    }
    if ((uint32_t)(now_ms - last_diagnostics_ms) >= 200U) {
      last_diagnostics_ms = now_ms;
      Diagnostics_Task(now_ms);
    }
  }
}


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  StartupSafety_EnterFatal(Error_FatalLoop);
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
