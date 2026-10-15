#ifndef RC_INPUT_H
#define RC_INPUT_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef struct {
    uint32_t ch1_us;
    uint32_t ch2_us;
    uint32_t ch5_us;
    uint32_t ch1_period_us;
    uint32_t ch2_period_us;
    uint32_t ch5_period_us;
    uint8_t ch1_valid;
    uint8_t ch2_valid;
    uint8_t ch5_valid;
    uint8_t ch1_fresh;
    uint8_t ch2_fresh;
    uint8_t ch5_fresh;
} RcInputSnapshot;

enum {
    RC_INPUT_CHANNEL_CH1 = 1U,
    RC_INPUT_CHANNEL_CH2 = 2U,
    RC_INPUT_CHANNEL_CH5 = 5U
};

void RcInput_Init(void);
HAL_StatusTypeDef RcInput_Start(void);
void RcInput_GetSnapshot(RcInputSnapshot *snapshot);
void RcInput_InvalidateOvercapture(uint32_t channel);

#endif /* RC_INPUT_H */
