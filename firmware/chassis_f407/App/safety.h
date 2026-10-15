#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

typedef struct {
    uint8_t ch1_fresh;
    uint8_t ch2_fresh;
    uint8_t ch5_fresh;
    uint32_t ch1_us;
    uint32_t ch2_us;
    uint32_t ch5_us;
} SafetyInputs;

typedef enum {
    SAFETY_LOCKED,
    SAFETY_WAIT_NEUTRAL,
    SAFETY_ARMED,
    SAFETY_FAILSAFE
} SafetyState;

typedef struct {
    SafetyState state;
    uint32_t neutral_start_ms;
} SafetyStateMachine;

void Safety_Init(SafetyStateMachine *machine);
void Safety_Update(SafetyStateMachine *machine, const SafetyInputs *inputs, uint32_t now_ms);
uint8_t Safety_IsDriveAllowed(const SafetyStateMachine *machine);

#endif
