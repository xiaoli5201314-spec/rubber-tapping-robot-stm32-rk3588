#include "wifi_test.h"
#include "board.h"
#include "at_parser.h"
#include "test_config.h"
#include "lcd_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EV_OK       1U
#define EV_ERROR    2U
#define EV_PROMPT   4U
#define EV_SENT     8U

volatile WifiTestStatus g_wifi_test_status;
static uint32_t s_events;
static uint32_t s_error_baseline;
static uint32_t s_parser_baseline;
static char s_device[25];
static char s_ip[40] = "unknown";
static char s_http[2048];
static uint16_t s_http_used;
static uint8_t s_http_overflow;
static char s_http_token[33];
static char s_cmd_topic[96];
static char s_echo_topic[96];
static char s_status_topic[96];
static char s_echo_nonce[33];
static uint8_t s_echo_pending;
static char s_last_echo_nonce[33];
static char s_command[256];
static uint32_t s_last_screen_service;

static int is_nonce(const char *text)
{
    uint32_t i;
    if (strlen(text) != 32U) return 0;
    for (i = 0U; i < 32U; ++i)
    {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static void on_mqtt(const char *line)
{
    const char *topic;
    const char *end;
    const char *payload;
    char *length_end;
    unsigned long length;
    size_t topic_len;

    if (strncmp(line, "+MQTTSUBRECV:0,\"", 16U) != 0) return;
    topic = line + 16U;
    end = strchr(topic, '"');
    if (end == NULL || end[1] != ',') return;
    topic_len = (size_t)(end - topic);
    if (strlen(s_cmd_topic) != topic_len ||
        strncmp(topic, s_cmd_topic, topic_len) != 0) return;
    length = strtoul(end + 2U, &length_end, 10);
    if (length_end == end + 2U || *length_end != ',') return;
    payload = length_end + 1U;
    if (length != strlen(payload)) return;

    if (length == 42U && strncmp(payload, "CHALLENGE:", 10U) == 0 &&
        is_nonce(payload + 10U))
    {
        (void)strcpy(s_echo_nonce, payload + 10U);
        s_echo_pending = 1U;
    }
    else if (length == 41U && strncmp(payload, "VERIFIED:", 9U) == 0 &&
             strcmp(payload + 9U, s_last_echo_nonce) == 0 &&
             is_nonce(payload + 9U))
    {
        g_wifi_test_status.host_verified = 1U;
        Board_DebugLine("[PASS] Computer verified HTTP token and MQTT round trip.");
    }
}

static void on_line(const char *line)
{
    if (strcmp(line, "OK") == 0) s_events |= EV_OK;
    if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0 ||
        strcmp(line, "SEND FAIL") == 0 ||
        strncmp(line, "busy ", 5U) == 0)
        s_events |= EV_ERROR;
    if (strcmp(line, "SEND OK") == 0) s_events |= EV_SENT;
    if (strcmp(line, "WIFI DISCONNECT") == 0)
    {
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.http_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        g_wifi_test_status.host_verified = 0U;
        s_ip[0] = '\0';
    }
    if (strncmp(line, "+MQTTDISCONNECTED:", 18U) == 0)
    {
        g_wifi_test_status.mqtt_ok = 0U;
        g_wifi_test_status.host_verified = 0U;
    }
    if (strncmp(line, "+CIFSR:STAIP,\"", 14U) == 0)
    {
        const char *end = strchr(line + 14U, '"');
        if (end != NULL && (size_t)(end - (line + 14U)) < sizeof(s_ip))
        {
            const size_t count = (size_t)(end - (line + 14U));
            (void)memcpy(s_ip, line + 14U, count);
            s_ip[count] = '\0';
        }
    }
    if (strncmp(line, "+MQTTSUBRECV:", 13U) == 0)
    {
        on_mqtt(line);
    }
    else if (strncmp(line, "AT+CWJAP", 8U) == 0)
    {
        Board_DebugLine("[ESP] Wi-Fi join command echo hidden.");
    }
    else
    {
        Board_Debug("[ESP] ");
        Board_DebugLine(line);
    }
}

static void on_data(uint8_t byte)
{
    if (s_http_used < sizeof(s_http) - 1U)
    {
        s_http[s_http_used++] = (char)byte;
        s_http[s_http_used] = '\0';
    }
    else
    {
        s_http_overflow = 1U;
    }
}

static void on_prompt(void)
{
    s_events |= EV_PROMPT;
}

static void service(void)
{
    int byte;
    uint8_t err;
    uint8_t run;
    uint32_t now;
    WifiDisplaySnapshot snapshot;
    while ((byte = Board_WifiRead()) >= 0)
    {
        AtParser_Feed((uint8_t)byte);
    }
    g_wifi_test_status.rx_errors = Board_WifiRxErrors();
    g_wifi_test_status.parser_errors = AtParser_Errors();
    err = (uint8_t)(g_wifi_test_status.stage == 99U ||
          g_wifi_test_status.rx_errors != s_error_baseline ||
          g_wifi_test_status.parser_errors != s_parser_baseline ||
          (g_wifi_test_status.stage >= 4U &&
           !g_wifi_test_status.http_ok) ||
          (g_wifi_test_status.stage >= 5U &&
           !g_wifi_test_status.mqtt_ok));
    run = (uint8_t)((Board_Millis() / 400U) & 1U);
    if (g_wifi_test_status.host_verified &&
        g_wifi_test_status.http_ok && g_wifi_test_status.mqtt_ok && !err)
        run = 1U;
    Board_Leds(err, run);
    now = Board_Millis();
    if ((uint32_t)(now - s_last_screen_service) >= 25U)
    {
        s_last_screen_service = now;
        WifiTest_GetSnapshot(&snapshot);
        LCD_Display_Service(&snapshot, now);
    }
}

void WifiTest_GetSnapshot(WifiDisplaySnapshot *snapshot)
{
    snapshot->status = g_wifi_test_status;
    snapshot->run_rx_errors = Board_WifiRxErrors() - s_error_baseline;
    snapshot->run_parser_errors = AtParser_Errors() - s_parser_baseline;
    (void)snprintf(snapshot->ip, sizeof(snapshot->ip), "%s",
                   g_wifi_test_status.wifi_ok ? s_ip : "");
}

static void pause_ms(uint32_t ms)
{
    const uint32_t start = Board_Millis();
    while ((uint32_t)(Board_Millis() - start) < ms) service();
}

static int wait_event(uint32_t event, uint32_t timeout)
{
    const uint32_t start = Board_Millis();
    while ((uint32_t)(Board_Millis() - start) < timeout)
    {
        service();
        if ((s_events & EV_ERROR) != 0U) return 0;
        if (Board_WifiRxErrors() != s_error_baseline ||
            AtParser_Errors() != s_parser_baseline) return 0;
        if ((s_events & event) != 0U) return 1;
    }
    return 0;
}

static int command(const char *text, uint32_t expected, uint32_t timeout)
{
    service();
    s_events = 0U;
    Board_WifiWrite(text);
    Board_WifiWrite("\r\n");
    return wait_event(expected, timeout);
}

static int probe_module(void)
{
    const uint32_t bauds[] = {115200U, 9600U, 57600U, 38400U,
                              19200U, 74880U};
    uint32_t i;
    uint32_t attempt;
    char log[80];
    for (i = 0U; i < sizeof(bauds) / sizeof(bauds[0]); ++i)
    {
        Board_WifiBaud(bauds[i]);
        AtParser_Reset();
        pause_ms(100U);
        s_error_baseline = Board_WifiRxErrors();
        s_parser_baseline = AtParser_Errors();
        for (attempt = 0U; attempt < 2U; ++attempt)
        {
            if (command("AT", EV_OK, 700U))
            {
                g_wifi_test_status.at_baud = bauds[i];
                g_wifi_test_status.at_ok = 1U;
                (void)snprintf(log, sizeof(log), "[PASS] AT baud=%lu",
                               (unsigned long)bauds[i]);
                Board_DebugLine(log);
                return command("ATE0", EV_OK, 1500U);
            }
            AtParser_Reset();
            s_error_baseline = Board_WifiRxErrors();
            s_parser_baseline = AtParser_Errors();
        }
    }
    return 0;
}

static int join_hotspot(void)
{
    Board_DebugLine("[TEST] Joining hotspot YOUR_WIFI_SSID...");
    (void)command("AT+GMR", EV_OK, 2000U);
    (void)command("AT+SYSSTORE=0", EV_OK, 1500U);
    if (!command("AT+CWMODE=1", EV_OK, 2000U) &&
        !command("AT+CWMODE_CUR=1", EV_OK, 2000U)) return 0;
    (void)snprintf(s_command, sizeof(s_command), "AT+CWJAP=\"%s\",\"%s\"",
                   WIFI_SSID, WIFI_PASSWORD);
    if (!command(s_command, EV_OK, 45000U)) return 0;
    g_wifi_test_status.wifi_ok = 1U;
    (void)command("AT+CIFSR", EV_OK, 3000U);
    Board_Debug("[PASS] Wi-Fi connected, IP=");
    Board_DebugLine(s_ip);
    return 1;
}

static int validate_http(void)
{
    const char *header_end;
    const char *marker;
    uint32_t i;
    if (s_http_overflow || strncmp(s_http, "HTTP/1.1 200 ", 13U) != 0)
        return 0;
    header_end = strstr(s_http, "\r\n\r\n");
    if (header_end == NULL) return 0;
    marker = header_end + 4U;
    if (strncmp(marker, "F407_HTTP_OK:", 13U) != 0 ||
        strlen(marker + 13U) < 32U) return 0;
    for (i = 0U; i < 32U; ++i) s_http_token[i] = marker[13U + i];
    s_http_token[32] = '\0';
    return is_nonce(s_http_token);
}

static int test_http(void)
{
    char request[224];
    uint32_t start;
    int ok = 0;
    Board_DebugLine("[TEST] HTTP GET and fresh server token...");
    g_wifi_test_status.http_ok = 0U;
    g_wifi_test_status.host_verified = 0U;
    g_wifi_test_status.http_busy = 1U;
    g_wifi_test_status.http_attempted = 1U;
    s_http_token[0] = '\0';
    (void)command("AT+CIPCLOSE", EV_OK, 1000U);
    if (!command("AT+CIPMUX=0", EV_OK, 1500U) ||
        !command("AT+CIPMODE=0", EV_OK, 1500U) ||
        !command("AT+CIPDINFO=0", EV_OK, 1500U)) goto done;
    /* Firmware may not provide this optional command; default mode is active. */
    (void)command("AT+CIPRECVMODE=0", EV_OK, 1500U);
    s_http_used = 0U;
    s_http[0] = '\0';
    s_http_overflow = 0U;
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+CIPSTART=\"TCP\",\"%s\",%u",
                   TEST_SERVER_HOST, TEST_HTTP_PORT);
    if (!command(s_command, EV_OK, 10000U)) goto done;
    (void)snprintf(request, sizeof(request),
                   "GET /probe?device=%s HTTP/1.1\r\n"
                   "Host: %s:%u\r\nConnection: close\r\n\r\n",
                   s_device, TEST_SERVER_HOST, TEST_HTTP_PORT);
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+CIPSEND=%u", (unsigned int)strlen(request));
    if (command(s_command, EV_PROMPT, 3000U))
    {
        s_events = 0U;
        Board_WifiWrite(request);
        if (wait_event(EV_SENT, 8000U))
        {
            start = Board_Millis();
            while ((uint32_t)(Board_Millis() - start) < 8000U)
            {
                service();
                if (validate_http())
                {
                    ok = 1;
                    break;
                }
            }
        }
    }
    (void)command("AT+CIPCLOSE", EV_OK, 1000U);
done:
    if (Board_WifiRxErrors() != s_error_baseline ||
        AtParser_Errors() != s_parser_baseline) ok = 0;
    g_wifi_test_status.http_ok = (uint32_t)ok;
    g_wifi_test_status.http_busy = 0U;
    if (!ok) g_wifi_test_status.last_failure = WIFI_FAILURE_HTTP;
    else if (g_wifi_test_status.last_failure == WIFI_FAILURE_HTTP)
        g_wifi_test_status.last_failure = WIFI_FAILURE_NONE;
    if (!ok) s_http_token[0] = '\0';
    Board_DebugLine(ok ? "[PASS] HTTP 200 and server token received." :
                         "[FAIL] HTTP; will retry. Check host/IP/hotspot isolation.");
    return ok;
}

static int setup_mqtt(void)
{
    Board_DebugLine("[TEST] MQTT connect and subscribe...");
    g_wifi_test_status.mqtt_ok = 0U;
    g_wifi_test_status.host_verified = 0U;
    g_wifi_test_status.mqtt_busy = 1U;
    g_wifi_test_status.mqtt_attempted = 1U;
    (void)command("AT+MQTTCLEAN=0", EV_OK, 3000U);
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+MQTTUSERCFG=0,1,\"f407_%s\",\"\",\"\",0,0,\"\"",
                   s_device);
    if (!command(s_command, EV_OK, 3000U)) goto failed;
    if (!command("AT+MQTTCONNCFG=0,30,0,\"\",\"\",0,0",
                 EV_OK, 3000U)) goto failed;
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+MQTTCONN=0,\"%s\",%u,0",
                   TEST_SERVER_HOST, TEST_MQTT_PORT);
    if (!command(s_command, EV_OK, 12000U)) goto failed;
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+MQTTSUB=0,\"%s\",1", s_cmd_topic);
    if (!command(s_command, EV_OK, 5000U)) goto failed;
    g_wifi_test_status.mqtt_ok = 1U;
    g_wifi_test_status.mqtt_busy = 0U;
    if (g_wifi_test_status.last_failure == WIFI_FAILURE_MQTT)
        g_wifi_test_status.last_failure = WIFI_FAILURE_NONE;
    Board_DebugLine("[PASS] MQTT connected; awaiting computer challenge.");
    return 1;
failed:
    g_wifi_test_status.mqtt_busy = 0U;
    g_wifi_test_status.last_failure = WIFI_FAILURE_MQTT;
    return 0;
}

static int mqtt_publish(const char *topic, const char *payload)
{
    const int count = snprintf(s_command, sizeof(s_command),
                    "AT+MQTTPUB=0,\"%s\",\"%s\",1,0", topic, payload);
    if (count < 0 || (size_t)count >= sizeof(s_command)) return 0;
    return command(s_command, EV_OK, 6000U);
}

static void publish_status(void)
{
    char payload[160];
    (void)snprintf(payload, sizeof(payload),
        "http=%lu;token=%s;ip=%s;rxerr=%lu;parseerr=%lu;fw=lcd1;drawms=%lu",
        (unsigned long)g_wifi_test_status.http_ok,
        s_http_token, s_ip,
        (unsigned long)(Board_WifiRxErrors() - s_error_baseline),
        (unsigned long)(AtParser_Errors() - s_parser_baseline),
        (unsigned long)g_lcd_max_field_ms);
    if (!mqtt_publish(s_status_topic, payload))
    {
        g_wifi_test_status.mqtt_ok = 0U;
        g_wifi_test_status.host_verified = 0U;
        g_wifi_test_status.last_failure = WIFI_FAILURE_MQTT;
        Board_DebugLine("[FAIL] MQTT publish; reconnecting later.");
    }
}

static void send_echo(void)
{
    char payload[48];
    s_echo_pending = 0U;
    (void)strcpy(s_last_echo_nonce, s_echo_nonce);
    (void)snprintf(payload, sizeof(payload), "ECHO:%s", s_last_echo_nonce);
    if (mqtt_publish(s_echo_topic, payload))
    {
        ++g_wifi_test_status.mqtt_echoes;
        Board_DebugLine("[PASS] MQTT challenge received and echo sent.");
    }
    else
    {
        g_wifi_test_status.mqtt_ok = 0U;
        g_wifi_test_status.host_verified = 0U;
        g_wifi_test_status.last_failure = WIFI_FAILURE_MQTT;
    }
}

void WifiTest_Run(void)
{
    uint32_t last_http;
    uint32_t last_status;
    uint32_t last_mqtt;
    uint32_t now;
    Board_DeviceId(s_device);
    (void)snprintf(s_cmd_topic, sizeof(s_cmd_topic), "%s%s/cmd",
                   TEST_TOPIC_PREFIX, s_device);
    (void)snprintf(s_echo_topic, sizeof(s_echo_topic), "%s%s/echo",
                   TEST_TOPIC_PREFIX, s_device);
    (void)snprintf(s_status_topic, sizeof(s_status_topic), "%s%s/status",
                   TEST_TOPIC_PREFIX, s_device);
    AtParser_Init(on_line, on_data, on_prompt);
    Board_DebugLine("\r\n=== F407 ESP-12F Wi-Fi / HTTP / MQTT + portrait LCD test ===");
    Board_Debug("Device: ");
    Board_DebugLine(s_device);
    Board_Debug("Test server: ");
    Board_DebugLine(TEST_SERVER_HOST);
    pause_ms(2500U);

    while (1)
    {
        g_wifi_test_status.at_ok = 0U;
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.http_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        g_wifi_test_status.host_verified = 0U;
        g_wifi_test_status.stage = 1U;
        g_wifi_test_status.at_baud = 0U;
        g_wifi_test_status.http_busy = 0U;
        g_wifi_test_status.mqtt_busy = 0U;
        g_wifi_test_status.http_attempted = 0U;
        g_wifi_test_status.mqtt_attempted = 0U;
        g_wifi_test_status.last_failure = WIFI_FAILURE_NONE;
        s_ip[0] = '\0';
        s_echo_pending = 0U;
        if (!probe_module())
        {
            g_wifi_test_status.stage = 99U;
            g_wifi_test_status.last_failure = WIFI_FAILURE_AT;
            Board_DebugLine("[FAIL] No reliable AT reply; check module power/boot/UART.");
            pause_ms(10000U);
            continue;
        }
        g_wifi_test_status.stage = 2U;
        if (!join_hotspot())
        {
            g_wifi_test_status.stage = 99U;
            g_wifi_test_status.last_failure = WIFI_FAILURE_HOTSPOT;
            Board_DebugLine("[FAIL] Wi-Fi join; check hotspot 2.4 GHz and module boot.");
            pause_ms(10000U);
            continue;
        }
        g_wifi_test_status.stage = 3U;
        (void)test_http();
        last_http = Board_Millis();
        g_wifi_test_status.stage = 4U;
        (void)setup_mqtt();
        last_mqtt = Board_Millis();
        last_status = Board_Millis() - 5000U;
        g_wifi_test_status.stage = 5U;

        while (g_wifi_test_status.wifi_ok)
        {
            service();
            now = Board_Millis();
            if (Board_WifiRxErrors() != s_error_baseline ||
                AtParser_Errors() != s_parser_baseline)
            {
                Board_DebugLine("[FAIL] UART/parser lost data; restart AT detection.");
                g_wifi_test_status.last_failure = WIFI_FAILURE_SERIAL;
                break;
            }
            if (!g_wifi_test_status.mqtt_ok &&
                (uint32_t)(now - last_mqtt) >= 10000U)
            {
                (void)setup_mqtt();
                last_mqtt = Board_Millis();
                last_status = last_mqtt - 5000U;
            }
            if (!g_wifi_test_status.http_ok &&
                (uint32_t)(now - last_http) >= 30000U)
            {
                (void)test_http();
                last_http = Board_Millis();
            }
            if (g_wifi_test_status.mqtt_ok && s_echo_pending) send_echo();
            if (g_wifi_test_status.mqtt_ok &&
                (uint32_t)(now - last_status) >= 5000U)
            {
                publish_status();
                last_status = Board_Millis();
            }
        }
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.http_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        g_wifi_test_status.host_verified = 0U;
        if (g_wifi_test_status.last_failure != WIFI_FAILURE_SERIAL)
            g_wifi_test_status.last_failure = WIFI_FAILURE_HOTSPOT;
        s_ip[0] = '\0';
        g_wifi_test_status.stage = 99U;
        pause_ms(5000U);
    }
}
