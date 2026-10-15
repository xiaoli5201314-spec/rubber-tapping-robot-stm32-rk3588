#ifndef UART4_TEST_H
#define UART4_TEST_H
#include <stdint.h>
#include "uart4_io.h"

typedef struct {
    Uart4Stats io;
    uint32_t heartbeat;
    uint32_t received_messages;
    uint32_t line_errors;
} Uart4TestStatus;

extern volatile Uart4TestStatus g_uart4_test_status;
void Uart4Test_Init(void);
void Uart4Test_Feed(uint8_t byte, uint32_t now);
void Uart4Test_Service(uint32_t now);

#endif
