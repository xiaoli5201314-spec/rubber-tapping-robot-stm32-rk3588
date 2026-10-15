#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>

void Board_Init(void);
uint32_t Board_Millis(void);
void Board_Leds(uint8_t err_on, uint8_t run_on);
void Board_Debug(const char *text);
void Board_DebugLine(const char *text);
void Board_WifiBaud(uint32_t baud);
void Board_WifiWrite(const char *text);
int Board_WifiRead(void);
uint32_t Board_WifiRxErrors(void);
void Board_DeviceId(char *out);

#endif
