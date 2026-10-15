#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include "motor_profile.h"

void MotorDriver_Init(void);
void MotorDriver_ApplyLeft(MotorOutput output);
void MotorDriver_ApplyRight(MotorOutput output);
void MotorDriver_EmergencyBrake(void);

#endif /* MOTOR_DRIVER_H */
