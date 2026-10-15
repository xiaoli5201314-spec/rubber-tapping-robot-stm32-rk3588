#include "uart4_test.h"
#include <stdio.h>
#include <string.h>

#define LINE_CAPACITY 192U
#define IDLE_FRAME_MS 80U

volatile Uart4TestStatus g_uart4_test_status;
static char s_line[LINE_CAPACITY];
static uint16_t s_line_used;
static uint8_t s_discard_line;
static uint32_t s_last_rx;
static uint32_t s_last_heartbeat;

static void send_status(const char *prefix)
{
    Uart4Stats stats;
    char output[192];
    Uart4_GetStats(&stats);
    g_uart4_test_status.io = stats;
    (void)snprintf(output, sizeof(output),
        "%s tick=%lu uptime_ms=%lu rx=%lu tx=%lu messages=%lu "
        "uarterr=%lu overflow=%lu lineerr=%lu txtimeout=%lu\r\n",
        prefix,
        (unsigned long)g_uart4_test_status.heartbeat,
        (unsigned long)Uart4_Millis(),
        (unsigned long)stats.rx_bytes,
        (unsigned long)stats.tx_bytes,
        (unsigned long)g_uart4_test_status.received_messages,
        (unsigned long)stats.uart_errors,
        (unsigned long)stats.buffer_overflows,
        (unsigned long)g_uart4_test_status.line_errors,
        (unsigned long)stats.tx_timeouts);
    (void)Uart4_Write(output);
}

static void finish_line(void)
{
    if (s_discard_line)
    {
        (void)Uart4_Write("ERROR: input too long or contains NUL; frame dropped\r\n");
    }
    else if (s_line_used != 0U)
    {
        s_line[s_line_used] = '\0';
        ++g_uart4_test_status.received_messages;
        if (strcmp(s_line, "PING") == 0)
        {
            (void)Uart4_Write("PONG\r\n");
        }
        else if (strcmp(s_line, "STATUS") == 0)
        {
            send_status("STATUS");
        }
        else if (strcmp(s_line, "HELP") == 0)
        {
            (void)Uart4_Write("HELP: PING -> PONG; STATUS -> counters; "
                              "other text -> ECHO. CR/LF or 80ms idle ends a frame.\r\n");
        }
        else
        {
            (void)Uart4_Write("ECHO: ");
            (void)Uart4_Write(s_line);
            (void)Uart4_Write("\r\n");
        }
    }
    s_line_used = 0U;
    s_discard_line = 0U;
}

void Uart4Test_Feed(uint8_t byte, uint32_t now)
{
    s_last_rx = now;
    if (byte == '\r' || byte == '\n')
    {
        finish_line();
    }
    else if (!s_discard_line)
    {
        if (byte == 0U || s_line_used >= LINE_CAPACITY - 1U)
        {
            ++g_uart4_test_status.line_errors;
            s_discard_line = 1U;
        }
        else
        {
            s_line[s_line_used++] = (char)byte;
        }
    }
}

void Uart4Test_Init(void)
{
    (void)memset((void *)&g_uart4_test_status, 0, sizeof(g_uart4_test_status));
    s_line_used = 0U;
    s_discard_line = 0U;
    s_last_rx = Uart4_Millis();
    s_last_heartbeat = s_last_rx;
    (void)Uart4_Write("\r\n=== STM32F407 UART4 TEST ===\r\n"
                      "PC10 TX / PC11 RX / CH340N TYPE-C\r\n"
                      "115200 baud, 8N1, no flow control\r\n"
                      "Send PING, STATUS, HELP, or text to echo.\r\n");
}

void Uart4Test_Service(uint32_t now)
{
    Uart4Stats stats;
    if ((s_line_used || s_discard_line) &&
        (uint32_t)(now - s_last_rx) >= IDLE_FRAME_MS)
    {
        finish_line();
    }
    /* Keep the heartbeat out of the middle of a user's partial message. */
    if (!s_line_used && !s_discard_line &&
        (uint32_t)(now - s_last_heartbeat) >= 1000U)
    {
        s_last_heartbeat = now;
        ++g_uart4_test_status.heartbeat;
        send_status("UART4");
    }
    Uart4_GetStats(&stats);
    g_uart4_test_status.io = stats;
    Uart4_UpdateLeds((uint8_t)(g_uart4_test_status.line_errors != 0U));
}
