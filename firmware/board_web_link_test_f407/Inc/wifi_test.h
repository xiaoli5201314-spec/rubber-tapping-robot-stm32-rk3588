#ifndef WIFI_TEST_H
#define WIFI_TEST_H
#include <stdint.h>

typedef enum
{
    WIFI_FAILURE_NONE = 0,
    WIFI_FAILURE_AT,
    WIFI_FAILURE_HOTSPOT,
    WIFI_FAILURE_SESSION,
    WIFI_FAILURE_MQTT,
    WIFI_FAILURE_SERIAL
} WifiTestFailure;

/* Visible in the Keil debugger; success requires real board/server traffic. */
typedef struct
{
    uint32_t at_baud;
    uint32_t rx_errors;
    uint32_t parser_errors;
    uint32_t stage;
    uint32_t at_ok;
    uint32_t wifi_ok;
    uint32_t session_ok;
    uint32_t mqtt_ok;
    uint32_t host_verified;
    uint32_t mqtt_echoes;
    uint32_t session_busy;
    uint32_t mqtt_busy;
    uint32_t session_attempted;
    uint32_t mqtt_attempted;
    uint32_t last_failure;
    uint32_t seq;
    uint32_t cmdcount;
    uint32_t runled;
} WifiTestStatus;

typedef struct
{
    WifiTestStatus status;
    uint32_t run_rx_errors;
    uint32_t run_parser_errors;
    char ip[16];
    char server[16];
} WifiDisplaySnapshot;

extern volatile WifiTestStatus g_wifi_test_status;
void WifiTest_GetSnapshot(WifiDisplaySnapshot *snapshot);
void WifiTest_Run(void);

#endif
