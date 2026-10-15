#ifndef WIFI_TEST_H
#define WIFI_TEST_H
#include <stdint.h>

/* Visible in the Keil debugger; success requires real board/server traffic. */
typedef struct
{
    uint32_t at_baud;
    uint32_t rx_errors;
    uint32_t parser_errors;
    uint32_t stage;
    uint32_t at_ok;
    uint32_t wifi_ok;
    uint32_t http_ok;
    uint32_t mqtt_ok;
    uint32_t host_verified;
    uint32_t mqtt_echoes;
} WifiTestStatus;

extern volatile WifiTestStatus g_wifi_test_status;
void WifiTest_Run(void);

#endif
