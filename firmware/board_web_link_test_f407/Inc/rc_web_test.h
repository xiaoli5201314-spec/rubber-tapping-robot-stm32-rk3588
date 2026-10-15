#ifndef RC_WEB_TEST_H
#define RC_WEB_TEST_H
#include <stdint.h>

typedef struct {
    uint32_t pulse[3];       /* CH1, CH2, CH5; microseconds */
    uint32_t period[3];      /* zero when invalid or stale */
    uint8_t valid[3];
    const char *state;       /* LOCKED, WAIT, ARMED, FAILSAFE */
    int16_t left;           /* signed permille command; not measured speed */
    int16_t right;
} RcWebSnapshot;

void RcWeb_Init(void);
void RcWeb_Service(uint32_t now_ms);
void RcWeb_Tick(uint32_t now_ms);
void RcWeb_GetSnapshot(RcWebSnapshot *snapshot);

#endif
