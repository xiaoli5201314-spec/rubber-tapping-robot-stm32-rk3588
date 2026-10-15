#ifndef MOTOR_WEB_H
#define MOTOR_WEB_H
#include <stdint.h>

/* Change one side only after an isolated, powered-off direction check. */
#define WEB_MOTOR_LEFT_INVERT  0U
#define WEB_MOTOR_RIGHT_INVERT 0U

uint8_t MotorWeb_Init(void);
void MotorWeb_Apply(int16_t left, int16_t right);
void MotorWeb_Stop(void);
void MotorWeb_FeedWatchdog(void);

#endif
