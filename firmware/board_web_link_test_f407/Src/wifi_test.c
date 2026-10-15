#include "wifi_test.h"
#include "board.h"
#include "at_parser.h"
#include "test_config.h"
#if !WEB_NO_LCD
#include "lcd_display.h"
#endif
#if WEB_RC_TEST
#include "rc_web_test.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EV_OK        1U
#define EV_ERROR     2U
#define EV_PROMPT    4U
#define EV_SENT      8U
#define EV_PUBLISHED 16U
#define EV_BUSY      32U
#define EV_MQTT_CONNECTED 64U
#define LED_PHASE_MS 80U
#define LED_EXPIRY_MS 5000U
#define PUB_DEADLINE_MS 10000U
#define AT_SETTLE_MS 20000U

volatile WifiTestStatus g_wifi_test_status;
static uint32_t s_events;
static uint32_t s_error_baseline;
static uint32_t s_parser_baseline;
static char s_device[25];
static char s_ip[16];
static char s_server[16];
static char s_session_token[33];
static char s_pub_session_token[33];
static char s_pub_payload[512];
static uint8_t s_pub_phase; /* 0 idle, 1 prompt, 2 result, 3 draining */
static uint8_t s_pub_echo;
static uint8_t s_pub_failures;
static uint8_t s_at_unsynced;
static uint8_t s_mqtt_connected_event;
static uint32_t s_pub_started;
static uint32_t s_rx_activity;
static char s_cmd_topic[96];
static char s_echo_topic[96];
static char s_status_topic[96];
static char s_echo_nonce[33];
/* 1 flashing, 2 awaiting ACK send, 3 ACK publishing, 4 awaiting status result. */
static uint8_t s_echo_pending;
static uint8_t s_accept_commands;
static uint32_t s_led_started;
static uint32_t s_led_phase_started;
static uint8_t s_led_phase;
/* A boot-lifetime Bloom filter rejects every previously accepted nonce.
   Collisions reject a new test safely; the computer can generate another ID. */
static uint8_t s_seen_nonces[1024];
static char s_command[256];
static void publish_service(uint32_t now);
#if !WEB_NO_LCD
static uint32_t s_last_screen_service;
#endif

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

static int is_ipv4(const char *text)
{
    uint32_t part;
    uint32_t any = 0U;
    for (part = 0U; part < 4U; ++part)
    {
        uint32_t number = 0U;
        uint32_t digits = 0U;
        while (*text >= '0' && *text <= '9')
        {
            number = number * 10U + (uint32_t)(*text++ - '0');
            if (++digits > 3U || number > 255U) return 0;
        }
        if (digits == 0U) return 0;
        any |= number;
        if (part < 3U)
        {
            if (*text++ != '.') return 0;
        }
        else if (*text != '\0') return 0;
    }
    return any != 0U;
}

static void clear_session(void)
{
    g_wifi_test_status.session_ok = 0U;
    (void)strcpy(s_session_token, "00000000000000000000000000000000");
}

static void cancel_led(void)
{
    s_echo_pending = 0U;
    s_led_phase = 0U;
    s_accept_commands = 0U;
    g_wifi_test_status.runled = 0U;
    g_wifi_test_status.host_verified = 0U;
}

static int nonce_seen_or_mark(const char *nonce)
{
    uint32_t h1 = 2166136261UL;
    uint32_t h2 = 5381U;
    uint32_t i;
    uint32_t seen = 1U;
    uint32_t positions[4];
    for (i = 0U; i < 32U; ++i)
    {
        h1 = (h1 ^ (uint8_t)nonce[i]) * 16777619UL;
        h2 = (h2 * 33U) ^ (uint8_t)nonce[i];
    }
    h2 |= 1U;
    for (i = 0U; i < 4U; ++i)
    {
        positions[i] = (h1 + i * h2) & 8191U;
        if ((s_seen_nonces[positions[i] / 8U] &
             (1U << (positions[i] % 8U))) == 0U) seen = 0U;
    }
    if (seen) return 1;
    for (i = 0U; i < 4U; ++i)
        s_seen_nonces[positions[i] / 8U] |=
            (uint8_t)(1U << (positions[i] % 8U));
    return 0;
}

static void on_mqtt(const char *line)
{
    const char *topic;
    const char *end;
    const char *payload;
    char *length_end;
    unsigned long length;
    size_t topic_len;
    uint8_t deferred = 0U;

    /* A fresh session challenge is allowed before the first heartbeat.
       LED commands stay blocked until a status publish completes, so
       reconnect-time retained/queued data cannot trigger a flash. */
    if (!g_wifi_test_status.wifi_ok || !g_wifi_test_status.mqtt_ok) return;
    if (strncmp(line, "+MQTTSUBRECV:0,\"", 16U) != 0) return;
    topic = line + 16U;
    end = strchr(topic, '"');
    if (end == NULL || end[1] != ',') return;
    topic_len = (size_t)(end - topic);
    if (strlen(s_cmd_topic) != topic_len ||
        strncmp(topic, s_cmd_topic, topic_len) != 0) return;
    if (end[2] < '0' || end[2] > '9') return;
    length = strtoul(end + 2U, &length_end, 10);
    if (length_end == end + 2U || *length_end != ',') return;
    payload = length_end + 1U;
    if (length == 40U && strlen(payload) == 40U &&
        strncmp(payload, "SESSION:", 8U) == 0 && is_nonce(payload + 8U))
    {
        if (strcmp(s_session_token, payload + 8U) != 0)
        {
            cancel_led();
            (void)strcpy(s_session_token, payload + 8U);
            g_wifi_test_status.session_ok = 1U;
            Board_DebugLine("[MQTT] Fresh computer session received; confirming in heartbeat.");
        }
        return;
    }
    if (!g_wifi_test_status.session_ok || s_echo_pending != 0U) return;
    if (length != 40U || strlen(payload) != 40U ||
        strncmp(payload, "LEDTEST:", 8U) != 0 ||
        !is_nonce(payload + 8U)) return;
    if (!s_accept_commands)
    {
        /* The host can receive our first matching heartbeat before the UART
           publish result arrives. Keep one request until that exact status
           succeeds; earlier sessions and echo transactions cannot queue it. */
        if (s_pub_phase != 2U || s_pub_echo ||
            strcmp(s_pub_session_token, s_session_token) != 0) return;
        deferred = 1U;
    }
    if (nonce_seen_or_mark(payload + 8U)) return;
    (void)strcpy(s_echo_nonce, payload + 8U);
    s_led_started = Board_Millis();
    s_led_phase = 0U;
    s_echo_pending = deferred ? 4U : 1U;
    g_wifi_test_status.host_verified = 0U;
    Board_DebugLine(deferred
        ? "[TEST] LED command queued until current status is confirmed."
        : "[TEST] LED command received; flash before ACK.");
}

static void query_ipv4(const char *line, const char *prefix, char *out)
{
    const size_t prefix_len = strlen(prefix);
    const char *end;
    char address[16];
    size_t length;
    if (strncmp(line, prefix, prefix_len) != 0) return;
    end = strchr(line + prefix_len, '"');
    if (end == NULL || end[1] != '\0') return;
    length = (size_t)(end - (line + prefix_len));
    if (length >= sizeof(address)) return;
    (void)memcpy(address, line + prefix_len, length);
    address[length] = '\0';
    if (is_ipv4(address)) (void)strcpy(out, address);
}

static void on_line(const char *line)
{
    if (strcmp(line, "OK") == 0) s_events |= EV_OK;
    if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0 ||
        strcmp(line, "SEND FAIL") == 0 ||
        strcmp(line, "+MQTTPUB:FAIL") == 0) s_events |= EV_ERROR;
    if (strncmp(line, "busy ", 5U) == 0) s_events |= EV_BUSY;
    if (strcmp(line, "SEND OK") == 0) s_events |= EV_SENT;
    if (strcmp(line, "+MQTTPUB:OK") == 0) s_events |= EV_PUBLISHED;
    if (strncmp(line, "+MQTTCONNECTED:",
                sizeof("+MQTTCONNECTED:") - 1U) == 0)
    {
        s_mqtt_connected_event = 1U;
        s_events |= EV_MQTT_CONNECTED;
    }
    if (strcmp(line, "ready") == 0)
    {
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        s_pub_phase = 0U;
        s_at_unsynced = 0U;
        clear_session();
        cancel_led();
        s_events |= EV_ERROR;
        Board_DebugLine("[RECOVERY] ESP restarted; rejoin hotspot.");
    }
    if (strcmp(line, "WIFI DISCONNECT") == 0)
    {
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        clear_session();
        cancel_led();
        s_ip[0] = '\0';
        s_server[0] = '\0';
        s_events |= EV_ERROR;
    }
    if (strncmp(line, "+MQTTDISCONNECTED:", 18U) == 0)
    {
        g_wifi_test_status.mqtt_ok = 0U;
        s_mqtt_connected_event = 0U;
        clear_session();
        cancel_led();
        s_events |= EV_ERROR;
    }
    query_ipv4(line, "+CIFSR:STAIP,\"", s_ip);
    query_ipv4(line, "+CIPSTA:ip:\"", s_ip);
    query_ipv4(line, "+CIPSTA_CUR:ip:\"", s_ip);
    if (TEST_SERVER_HOST[0] == '\0')
    {
        query_ipv4(line, "+CIPSTA:gateway:\"", s_server);
        query_ipv4(line, "+CIPSTA_CUR:gateway:\"", s_server);
    }
    if (strncmp(line, "+MQTTSUBRECV:", 13U) == 0) on_mqtt(line);
    else if (strncmp(line, "AT+CWJAP", 8U) == 0)
        Board_DebugLine("[ESP] Wi-Fi join command echo hidden.");
    else
    {
        Board_Debug("[ESP] ");
        Board_DebugLine(line);
    }
}

static void on_data(uint8_t byte) { (void)byte; }

static void on_prompt(void)
{
    s_events |= EV_PROMPT;
}

static void service(void)
{
    int byte;
    uint8_t err;
    uint8_t run = 0U;
    uint32_t now;
#if !WEB_NO_LCD
    WifiDisplaySnapshot snapshot;
#endif
    while ((byte = Board_WifiRead()) >= 0)
    {
        s_rx_activity = Board_Millis();
        AtParser_Feed((uint8_t)byte);
    }
    now = Board_Millis();
    publish_service(now);
#if WEB_RC_TEST && !WEB_MOTOR_TEST
    RcWeb_Service(now);
#endif
    g_wifi_test_status.rx_errors = Board_WifiRxErrors();
    g_wifi_test_status.parser_errors = AtParser_Errors();
    err = (uint8_t)(g_wifi_test_status.stage == 99U ||
          g_wifi_test_status.rx_errors != s_error_baseline ||
          g_wifi_test_status.parser_errors != s_parser_baseline ||
          (g_wifi_test_status.stage >= 5U && !g_wifi_test_status.mqtt_ok));
    if (s_echo_pending != 0U)
    {
        const uint32_t elapsed = now - s_led_started;
        if (!g_wifi_test_status.wifi_ok || !g_wifi_test_status.mqtt_ok ||
            elapsed >= LED_EXPIRY_MS || err)
        {
            s_echo_pending = 0U;
        }
        else if (s_echo_pending == 1U)
        {
            if (s_led_phase == 0U)
            {
                s_led_phase = 1U;
                s_led_phase_started = now;
            }
            else if ((uint32_t)(now - s_led_phase_started) >= LED_PHASE_MS)
            {
                ++s_led_phase;
                s_led_phase_started = now;
            }
            if (s_led_phase >= 4U)
            {
                s_echo_pending = 2U;
                ++g_wifi_test_status.cmdcount;
            }
            else run = (uint8_t)(s_led_phase == 1U || s_led_phase == 3U);
        }
    }
    /* PF10 has one owner: a requested LED flash, otherwise off. */
    g_wifi_test_status.runled = run;
    Board_Leds(err, run);
#if !WEB_NO_LCD
    if ((uint32_t)(now - s_last_screen_service) >= 25U)
    {
        s_last_screen_service = now;
        WifiTest_GetSnapshot(&snapshot);
        LCD_Display_Service(&snapshot, now);
    }
#endif
}

void WifiTest_GetSnapshot(WifiDisplaySnapshot *snapshot)
{
    snapshot->status = g_wifi_test_status;
    snapshot->run_rx_errors = Board_WifiRxErrors() - s_error_baseline;
    snapshot->run_parser_errors = AtParser_Errors() - s_parser_baseline;
    (void)snprintf(snapshot->ip, sizeof(snapshot->ip), "%s",
                   g_wifi_test_status.wifi_ok ? s_ip : "");
    (void)snprintf(snapshot->server, sizeof(snapshot->server), "%s",
                   g_wifi_test_status.wifi_ok ? s_server : "");
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
        if ((s_events & (EV_ERROR | EV_BUSY)) != 0U) return 0;
        if (Board_WifiRxErrors() != s_error_baseline ||
            AtParser_Errors() != s_parser_baseline) return 0;
        if ((s_events & event) != 0U) return 1;
    }
    return 0;
}

static int command(const char *text, uint32_t expected, uint32_t timeout)
{
    service();
    if (s_pub_phase != 0U || s_at_unsynced) return 0;
    s_events = 0U;
    Board_WifiWrite(text);
    Board_WifiWrite("\r\n");
    if (wait_event(expected, timeout)) return 1;
    /* A timeout is not a terminal reply. Do not send another AT command
       while the module may still be executing the previous one. */
    if ((s_events & EV_ERROR) == 0U) s_at_unsynced = 1U;
    return 0;
}

static void settle_at(void)
{
    const uint32_t start = Board_Millis();
    if (!s_at_unsynced) return;
    Board_DebugLine("[RECOVERY] Draining late AT responses before issuing new commands.");
    s_events = 0U;
    while ((uint32_t)(Board_Millis() - start) < AT_SETTLE_MS)
    {
        service();
        if ((s_events & (EV_OK | EV_ERROR | EV_PUBLISHED)) != 0U &&
            (uint32_t)(Board_Millis() - s_rx_activity) >= 250U) break;
    }
    /* A CRLF terminates an abandoned command line. RAW recovery first
       supplies the declared inert payload in publish_service(). */
    Board_WifiWrite("\r\n");
    pause_ms(300U);
    s_at_unsynced = 0U;
    s_events = 0U;
    AtParser_Reset();
    s_error_baseline = Board_WifiRxErrors();
    s_parser_baseline = AtParser_Errors();
}

static int probe_module(void)
{
    const uint32_t bauds[] = {115200U, 9600U, 57600U, 38400U, 19200U, 74880U};
    uint32_t i;
    uint32_t attempt;
    char log[80];
    for (i = 0U; i < sizeof(bauds) / sizeof(bauds[0]); ++i)
    {
        settle_at();
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
            settle_at();
            AtParser_Reset();
            s_error_baseline = Board_WifiRxErrors();
            s_parser_baseline = AtParser_Errors();
        }
    }
    return 0;
}

static int join_hotspot(void)
{
    Board_DebugLine("[TEST] Joining configured 2.4 GHz computer hotspot...");
    (void)command("AT+GMR", EV_OK, 2000U);
    (void)command("AT+SYSSTORE=0", EV_OK, 1500U);
    if (!command("AT+CWMODE=1", EV_OK, 2000U) &&
        !command("AT+CWMODE_CUR=1", EV_OK, 2000U)) return 0;
    (void)snprintf(s_command, sizeof(s_command), "AT+CWJAP=\"%s\",\"%s\"",
                   WIFI_SSID, WIFI_PASSWORD);
    if (!command(s_command, EV_OK, 45000U)) return 0;
    g_wifi_test_status.wifi_ok = 1U;
    (void)command("AT+CIFSR", EV_OK, 3000U);
    if (!command("AT+CIPSTA?", EV_OK, 3000U))
        (void)command("AT+CIPSTA_CUR?", EV_OK, 3000U);
    if (TEST_SERVER_HOST[0] != '\0' && is_ipv4(TEST_SERVER_HOST))
        (void)strcpy(s_server, TEST_SERVER_HOST);
    if (!is_ipv4(s_ip) || !is_ipv4(s_server))
    {
        Board_DebugLine("[FAIL] No valid station/gateway IPv4; retry hotspot.");
        return 0;
    }
    Board_Debug("[PASS] Wi-Fi IP=");
    Board_DebugLine(s_ip);
    Board_Debug("Computer server=");
    Board_DebugLine(s_server);
    return 1;
}

static int setup_mqtt(void)
{
    if (s_pub_phase != 0U || s_at_unsynced) return 0;
    Board_DebugLine("[TEST] MQTT connect and subscribe...");
    g_wifi_test_status.mqtt_ok = 0U;
    cancel_led();
    clear_session();
    g_wifi_test_status.mqtt_busy = 1U;
    g_wifi_test_status.mqtt_attempted = 1U;
    (void)command("AT+MQTTCLEAN=0", EV_OK, 10000U);
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+MQTTUSERCFG=0,1,\"f407_web_%s\",\"\",\"\",0,0,\"\"",
                   s_device);
    if (!command(s_command, EV_OK, 10000U)) goto failed;
    /* disable_clean_session=0 drops queued commands from earlier sessions. */
    if (!command("AT+MQTTCONNCFG=0,30,0,\"\",\"\",0,0", EV_OK, 10000U))
        goto failed;
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+MQTTCONN=0,\"%s\",%u,0", s_server, TEST_MQTT_PORT);
    /* Clear a stale notification before issuing the command.  ESP-AT may
       return OK and +MQTTCONNECTED in the same UART batch; clearing here
       (after command()) would lose that valid event. */
    s_mqtt_connected_event = 0U;
    if (!command(s_command, EV_OK, 30000U)) goto failed;
    /* ESP-AT acknowledges AT+MQTTCONN before the broker handshake finishes. */
    if (!wait_event(EV_MQTT_CONNECTED, 15000U) || !s_mqtt_connected_event)
        goto failed;
    (void)snprintf(s_command, sizeof(s_command),
                   "AT+MQTTSUB=0,\"%s\",0", s_cmd_topic);
    if (!command(s_command, EV_OK, 10000U)) goto failed;
    g_wifi_test_status.mqtt_ok = 1U;
    g_wifi_test_status.mqtt_busy = 0U;
    s_pub_failures = 0U;
    s_accept_commands = 0U;
    if (g_wifi_test_status.last_failure == WIFI_FAILURE_MQTT)
        g_wifi_test_status.last_failure = WIFI_FAILURE_NONE;
    Board_DebugLine("[PASS] MQTT connected; new heartbeat before LED tests.");
    return 1;
failed:
    g_wifi_test_status.mqtt_busy = 0U;
    g_wifi_test_status.last_failure = WIFI_FAILURE_MQTT;
    return 0;
}

static int mqtt_publish(const char *topic, const char *payload)
{
    int count;
    if (s_pub_phase != 0U || s_at_unsynced || !g_wifi_test_status.mqtt_ok ||
        strlen(payload) >= sizeof(s_pub_payload)) return 0;
    (void)strcpy(s_pub_payload, payload);
    (void)strcpy(s_pub_session_token, s_session_token);
    /* Latest-state heartbeats use QoS 0; LED confirmations use QoS 1.
       Never send retained data or append CRLF to the exact RAW payload. */
    s_pub_echo = (uint8_t)(strcmp(topic, s_echo_topic) == 0);
    count = snprintf(s_command, sizeof(s_command),
        "AT+MQTTPUBRAW=0,\"%s\",%u,%u,0", topic,
        (unsigned int)strlen(payload), (unsigned int)s_pub_echo);
    if (count < 0 || (size_t)count >= sizeof(s_command)) return 0;
    s_events = 0U;
    s_pub_phase = 1U;
    s_pub_started = Board_Millis();
    Board_WifiWrite(s_command);
    Board_WifiWrite("\r\n");
    return 1;
}

static void mqtt_failed(void)
{
    g_wifi_test_status.mqtt_ok = 0U;
    clear_session();
    cancel_led();
    g_wifi_test_status.last_failure = WIFI_FAILURE_MQTT;
    Board_DebugLine("[FAIL] MQTT publish; reconnecting.");
}

static void publish_service(uint32_t now)
{
    uint32_t elapsed;
    if (s_pub_phase == 0U) return;
    elapsed = now - s_pub_started;
    if (s_pub_phase == 3U)
    {
        /* Hold the transaction boundary until a terminal result arrives.
           Late publish results are discarded, never credited to a new send. */
        const uint8_t terminal = (uint8_t)(
            (s_events & (EV_PUBLISHED | EV_ERROR)) != 0U &&
            (uint32_t)(now - s_rx_activity) >= 250U);
        if (terminal || elapsed >= AT_SETTLE_MS)
        {
            s_pub_phase = 0U;
            if (terminal) s_at_unsynced = 0U;
            s_events = 0U;
            /* If there was no terminal reply, settle_at must still probe. */
        }
        return;
    }
    if ((s_events & EV_ERROR) != 0U && g_wifi_test_status.wifi_ok &&
        g_wifi_test_status.mqtt_ok)
    {
        /* An explicit failure completes this transaction. Retry a pending
           LEDACK with the same nonce; the LEDs are not flashed again. */
        s_pub_phase = 0U;
        s_events = 0U;
        if (s_echo_pending == 4U) s_echo_pending = 0U;
        if (s_pub_echo && s_echo_pending == 3U) s_echo_pending = 2U;
        ++s_pub_failures;
        Board_DebugLine("[MQTT] Explicit publish failure; retry after backoff.");
        if (s_pub_failures >= 3U) mqtt_failed();
        return;
    }
    if (s_pub_phase == 1U && (s_events & EV_PROMPT) != 0U)
    {
        s_events = 0U; /* OK and any pre-payload result cannot confirm this send. */
        s_pub_phase = 2U;
        s_pub_started = now;
        Board_WifiWrite(s_pub_payload);
        return;
    }
    if (s_pub_phase == 2U && (s_events & EV_PUBLISHED) != 0U &&
        g_wifi_test_status.mqtt_ok)
    {
        s_pub_phase = 0U;
        s_events = 0U;
        s_pub_failures = 0U;
        /* An old status or LEDACK may finish after a new SESSION arrives.
           Only a status that carries the current session enables tests. */
        if (!s_pub_echo && g_wifi_test_status.session_ok &&
            strcmp(s_pub_session_token, s_session_token) == 0)
        {
            s_accept_commands = 1U;
            if (s_echo_pending == 4U) s_echo_pending = 1U;
        }
        if (s_pub_echo && s_echo_pending == 3U)
        {
            s_echo_pending = 0U;
            ++g_wifi_test_status.mqtt_echoes;
            g_wifi_test_status.host_verified = 1U;
            Board_DebugLine("[PASS] Two LED flashes completed; matching LEDACK published.");
        }
        return;
    }
    if (elapsed >= PUB_DEADLINE_MS || (s_events & EV_BUSY) != 0U ||
        !g_wifi_test_status.mqtt_ok || !g_wifi_test_status.wifi_ok)
    {
        if (s_pub_phase == 1U)
        {
            /* If a prompt was lost, the ESP may still be waiting for RAW
               bytes. Fill exactly that length with an invalid heartbeat,
               before attempting any AT recovery. Never feed AT into RAW. */
            (void)memset(s_pub_payload, ' ', strlen(s_pub_payload));
            Board_WifiWrite(s_pub_payload);
        }
        s_pub_phase = 3U;
        s_pub_started = now;
        s_at_unsynced = 1U;
        s_events = 0U;
        mqtt_failed();
        Board_DebugLine("[RECOVERY] Publish deadline/disconnect; drain delayed response.");
    }
}

static void publish_status(void)
{
    char payload[512];
    int count;
    if (s_pub_phase != 0U || s_at_unsynced || !g_wifi_test_status.mqtt_ok) return;
#if WEB_RC_TEST
    RcWebSnapshot rc;
    RcWeb_GetSnapshot(&rc);
#endif
    ++g_wifi_test_status.seq;
    count = snprintf(payload, sizeof(payload),
        "fw=" WEB_FIRMWARE_VERSION ";mqtt=%lu;token=%s;ip=%s;server=%s;uptime=%lu;"
        "seq=%lu;runled=%lu;cmdcount=%lu;rxerr=%lu;parseerr=%lu",
        (unsigned long)g_wifi_test_status.mqtt_ok, s_session_token, s_ip, s_server,
        (unsigned long)(Board_Millis() / 1000U),
        (unsigned long)g_wifi_test_status.seq,
        (unsigned long)g_wifi_test_status.runled,
        (unsigned long)g_wifi_test_status.cmdcount,
        (unsigned long)(Board_WifiRxErrors() - s_error_baseline),
        (unsigned long)(AtParser_Errors() - s_parser_baseline));
#if WEB_RC_TEST
    if (count >= 0 && (size_t)count < sizeof(payload))
    {
        const int extra = snprintf(payload + count, sizeof(payload) - (size_t)count,
            ";ch1=%lu;ch2=%lu;ch5=%lu;p1=%lu;p2=%lu;p5=%lu;"
            "v1=%u;v2=%u;v5=%u;state=%s;left=%d;right=%d;motor=%u",
            (unsigned long)rc.pulse[0], (unsigned long)rc.pulse[1],
            (unsigned long)rc.pulse[2], (unsigned long)rc.period[0],
            (unsigned long)rc.period[1], (unsigned long)rc.period[2],
            (unsigned int)rc.valid[0], (unsigned int)rc.valid[1],
            (unsigned int)rc.valid[2], rc.state, (int)rc.left, (int)rc.right,
            (unsigned int)WEB_MOTOR_TEST);
        count = extra < 0 ? -1 : count + extra;
    }
#endif
    if (count < 0 || (size_t)count >= sizeof(payload) ||
        !mqtt_publish(s_status_topic, payload) || !g_wifi_test_status.mqtt_ok)
        mqtt_failed();
}

static void send_echo(void)
{
    char payload[48];
    if (s_echo_pending != 2U || s_pub_phase != 0U ||
        s_at_unsynced || !g_wifi_test_status.mqtt_ok) return;
    if ((uint32_t)(Board_Millis() - s_led_started) >= LED_EXPIRY_MS)
    {
        s_echo_pending = 0U;
        return;
    }
    (void)snprintf(payload, sizeof(payload), "LEDACK:%s:OK", s_echo_nonce);
    if (mqtt_publish(s_echo_topic, payload) && g_wifi_test_status.mqtt_ok)
        s_echo_pending = 3U;
    else mqtt_failed();
}

void WifiTest_Run(void)
{
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
    clear_session();
    AtParser_Init(on_line, on_data, on_prompt);
#if WEB_MOTOR_TEST
    Board_DebugLine("\r\n=== F407 " WEB_FIRMWARE_VERSION " / D60A motor test / no LCD ===");
#elif WEB_RC_TEST
    Board_DebugLine("\r\n=== F407 mqttrc1 / ESP-12F / RC web monitor (no LCD) ===");
#else
    Board_DebugLine("\r\n=== F407 mqttweb1 / ESP-12F / portrait LCD link test ===");
#endif
    Board_Debug("Device: ");
    Board_DebugLine(s_device);
#if WEB_MOTOR_TEST
#if MOTOR_MAX_OUTPUT_PERMILLE == 300
    Board_DebugLine("[MOTOR] Output enabled, max 30%; CH5 low + neutral 1s, then high.");
#elif MOTOR_MAX_OUTPUT_PERMILLE == 600
    Board_DebugLine("[MOTOR] Output enabled, max 60%; CH5 low + neutral 1s, then high.");
#else
    Board_DebugLine("[MOTOR] Output enabled, max 100%; CH5 low + neutral 1s, then high.");
#endif
#else
    Board_DebugLine("[SAFE] Known motor PWM/direction outputs held low.");
#endif
    pause_ms(2500U);
    while (1)
    {
        g_wifi_test_status.at_ok = 0U;
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        clear_session();
        cancel_led();
        g_wifi_test_status.stage = 1U;
        g_wifi_test_status.at_baud = 0U;
        g_wifi_test_status.session_busy = 0U;
        g_wifi_test_status.mqtt_busy = 0U;
        g_wifi_test_status.session_attempted = 0U;
        g_wifi_test_status.mqtt_attempted = 0U;
        g_wifi_test_status.last_failure = WIFI_FAILURE_NONE;
        s_ip[0] = '\0';
        s_server[0] = '\0';
        if (!probe_module())
        {
            g_wifi_test_status.stage = 99U;
            g_wifi_test_status.last_failure = WIFI_FAILURE_AT;
            Board_DebugLine("[FAIL] AT detection; check module power/boot/UART.");
            pause_ms(5000U);
            continue;
        }
        g_wifi_test_status.stage = 2U;
        if (!join_hotspot())
        {
            g_wifi_test_status.stage = 99U;
            g_wifi_test_status.last_failure = WIFI_FAILURE_HOTSPOT;
            Board_DebugLine("[FAIL] Hotspot/gateway; check PC hotspot 2.4 GHz.");
            pause_ms(5000U);
            continue;
        }
        g_wifi_test_status.stage = 4U;
        (void)setup_mqtt();
        last_mqtt = Board_Millis();
        last_status = last_mqtt - 1000U;
        g_wifi_test_status.stage = 5U;
        while (g_wifi_test_status.wifi_ok)
        {
            service();
            now = Board_Millis();
            if (Board_WifiRxErrors() != s_error_baseline ||
                AtParser_Errors() != s_parser_baseline)
            {
                g_wifi_test_status.last_failure = WIFI_FAILURE_SERIAL;
                Board_DebugLine("[FAIL] UART/parser lost data; detecting AT again.");
                break;
            }
            if (s_pub_phase == 0U && s_at_unsynced)
            {
                settle_at();
                if (!command("AT", EV_OK, 1500U)) break;
            }
            if (!g_wifi_test_status.mqtt_ok &&
                s_pub_phase == 0U && !s_at_unsynced &&
                (uint32_t)(now - last_mqtt) >= 2000U)
            {
                (void)setup_mqtt();
                last_mqtt = Board_Millis();
                last_status = last_mqtt - 1000U;
            }
            if (s_echo_pending == 2U &&
                (uint32_t)(now - s_pub_started) >=
                    (s_pub_failures ? 1000U : 0U)) send_echo();
            now = Board_Millis();
            if (g_wifi_test_status.mqtt_ok && s_echo_pending != 2U &&
                (uint32_t)(now - last_status) >= 1000U)
            {
                last_status = now;
                publish_status();
            }
        }
        /* Finish draining any abandoned RAW publish before AT probing. */
        while (s_pub_phase != 0U) service();
        settle_at();
        g_wifi_test_status.wifi_ok = 0U;
        g_wifi_test_status.mqtt_ok = 0U;
        clear_session();
        cancel_led();
        if (g_wifi_test_status.last_failure != WIFI_FAILURE_SERIAL)
            g_wifi_test_status.last_failure = WIFI_FAILURE_HOTSPOT;
        s_ip[0] = '\0';
        s_server[0] = '\0';
        g_wifi_test_status.stage = 99U;
        pause_ms(5000U);
    }
}
