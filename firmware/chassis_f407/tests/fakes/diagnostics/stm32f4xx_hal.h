#ifndef TEST_DIAGNOSTICS_STM32F4XX_HAL_H
#define TEST_DIAGNOSTICS_STM32F4XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY
} HAL_StatusTypeDef;

typedef enum {
    HAL_UART_STATE_RESET = 0x00U,
    HAL_UART_STATE_READY = 0x20U,
    HAL_UART_STATE_BUSY_TX = 0x21U
} HAL_UART_StateTypeDef;

typedef struct {
    HAL_UART_StateTypeDef gState;
} UART_HandleTypeDef;

HAL_UART_StateTypeDef HAL_UART_GetState(const UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                      const uint8_t *data,
                                      uint16_t length);

#endif /* TEST_DIAGNOSTICS_STM32F4XX_HAL_H */
