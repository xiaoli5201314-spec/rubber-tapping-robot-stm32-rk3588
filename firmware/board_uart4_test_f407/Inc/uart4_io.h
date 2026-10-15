#ifndef UART4_IO_H
#define UART4_IO_H
#include <stdint.h>

typedef struct {
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t uart_errors;
    uint32_t buffer_overflows;
    uint32_t tx_timeouts;
} Uart4Stats;

void Uart4_Init(void);
uint32_t Uart4_Millis(void);
int Uart4_Read(void);
int Uart4_Write(const char *text);
void Uart4_GetStats(Uart4Stats *stats);
void Uart4_UpdateLeds(uint8_t application_error);

#endif
