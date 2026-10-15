#include "lcd_display.h"
#include "lcd_driver.h"
#include "board.h"
#include "test_config.h"
#include <stdio.h>
#include <string.h>

#define RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                (((uint16_t)(g) & 0xFCU) << 3) | ((uint16_t)(b) >> 3)))
#define COLOR_BG          RGB565(238, 244, 240)
#define COLOR_HEADER      RGB565(20, 82, 58)
#define COLOR_HEADER_SUB  RGB565(184, 226, 207)
#define COLOR_CARD        LCD_COLOR_WHITE
#define COLOR_CARD_HEADER RGB565(222, 238, 229)
#define COLOR_TEXT        RGB565(24, 35, 30)
#define COLOR_MUTED       RGB565(78, 99, 89)
#define COLOR_BORDER      RGB565(157, 177, 166)
#define COLOR_VALUE       RGB565(0, 78, 162)
#define COLOR_GOOD        RGB565(0, 126, 72)
#define COLOR_WARNING     RGB565(180, 98, 0)
#define COLOR_ERROR       RGB565(190, 30, 48)

/* Numeric Unicode keeps the Keil source encoding independent of the fonts. */
#define ZH(name, ...) static const uint16_t name[] = {__VA_ARGS__, 0}
ZH(STR_TITLE, 0x7F51, 0x9875, 0x6D4B, 0x8BD5); /* 网页测试 */
ZH(STR_TEST_STATE, 0x6D4B, 0x8BD5, 0x72B6, 0x6001);
ZH(STR_NETWORK, 0x7F51, 0x7EDC, 0x8FDE, 0x63A5);
ZH(STR_HOST_CHECK, 0x81EA, 0x68C0, 0x72B6, 0x6001);
ZH(STR_ROUND_TRIP, 0x53CC, 0x5411, 0x56DE, 0x4F20);
ZH(STR_RUN_INFO, 0x8FD0, 0x884C, 0x4FE1, 0x606F);
ZH(STR_UART_ERROR, 0x4E32, 0x53E3, 0x9519, 0x8BEF);
ZH(STR_PARSER_ERROR, 0x89E3, 0x6790, 0x9519, 0x8BEF);
ZH(STR_BOOT, 0x542F, 0x52A8);
ZH(STR_DETECT, 0x68C0, 0x6D4B);
ZH(STR_CONNECT, 0x8FDE, 0x63A5);
ZH(STR_TEST, 0x6D4B, 0x8BD5);
ZH(STR_WAIT, 0x7B49, 0x5F85);
ZH(STR_PASS, 0x901A, 0x8FC7);
ZH(STR_ABNORMAL, 0x5F02, 0x5E38);
ZH(STR_RETRY, 0x91CD, 0x8BD5);
ZH(STR_NOT_CONNECTED, 0x672A, 0x8FDE, 0x63A5);
ZH(STR_DETECTING, 0x68C0, 0x6D4B, 0x4E2D);
ZH(STR_CONNECTING, 0x8FDE, 0x63A5, 0x4E2D);
ZH(STR_CONNECTED, 0x5DF2, 0x8FDE, 0x63A5);
ZH(STR_NOT_STARTED, 0x672A, 0x5F00, 0x59CB);
ZH(STR_TESTING, 0x6D4B, 0x8BD5, 0x4E2D);
ZH(STR_PASSED, 0x5DF2, 0x901A, 0x8FC7);
ZH(STR_WAIT_RETRY, 0x5F85, 0x91CD, 0x8BD5);
ZH(STR_WAIT_VERIFY, 0x5F85, 0x9A8C, 0x8BC1);
ZH(MSG_BOOT, 0x5C4F, 0x5E55, 0x542F, 0x52A8, 0xFF0C,
             0x51C6, 0x5907, 0x8054, 0x7F51);
ZH(MSG_DETECT, 0x6B63, 0x5728, 0x68C0, 0x6D4B, 0x6A21,
               0x5757, 0x4E32, 0x53E3);
ZH(MSG_JOIN, 0x6B63, 0x5728, 0x8FDE, 0x63A5, 0x7535,
             0x8111, 0x70ED, 0x70B9);
ZH(MSG_MQTT, 0x6B63, 0x5728, 0x8FDE, 0x63A5, 0x6D88,
             0x606F, 0x670D, 0x52A1);
ZH(MSG_WAIT, 0x7B49, 0x5F85, 0x7535, 0x8111, 0x53CC,
             0x5411, 0x901A, 0x4FE1, 0x9A8C, 0x8BC1);
ZH(MSG_PASS, 0x81EA, 0x68C0, 0x5DF2, 0x901A, 0x8FC7,
             0xFF0C, 0x5DF2, 0x56DE, 0x4F20, 0x7535, 0x8111);
ZH(MSG_HOST, 0x8BF7, 0x68C0, 0x67E5, 0x7535, 0x8111,
             0x6D4B, 0x8BD5, 0x670D, 0x52A1);
ZH(MSG_AT_FAIL, 0x6A21, 0x5757, 0x672A, 0x54CD, 0x5E94,
                0xFF0C, 0x81EA, 0x52A8, 0x91CD, 0x8BD5);
ZH(MSG_JOIN_FAIL, 0x70ED, 0x70B9, 0x8FDE, 0x63A5, 0x5931,
                  0x8D25, 0xFF0C, 0x81EA, 0x52A8, 0x91CD, 0x8BD5);
ZH(MSG_SERIAL_FAIL, 0x4E32, 0x53E3, 0x5F02, 0x5E38, 0xFF0C,
                    0x81EA, 0x52A8, 0x91CD, 0x8BD5);

typedef enum {
    FIELD_STATE, FIELD_MESSAGE, FIELD_WIFI, FIELD_IP, FIELD_SESSION,
    FIELD_MQTT, FIELD_VERIFY, FIELD_ECHO, FIELD_AT, FIELD_UPTIME,
    FIELD_UART_ERRORS, FIELD_PARSE_ERRORS, FIELD_SERVER, FIELD_SEQ, FIELD_COUNT
} DisplayField;

typedef struct {
    uint16_t x, y, w, h;
    uint8_t chinese_size;
    uint8_t ascii_scale;
} FieldGeometry;

static const FieldGeometry s_geometry[FIELD_COUNT] = {
    {336U, 132U, 112U, 48U, 48U, 0U},
    {32U, 186U, 416U, 24U, 24U, 0U},
    {352U, 282U, 96U, 24U, 24U, 0U},
    {104U, 328U, 344U, 32U, 0U, 2U},
    {32U, 438U, 184U, 32U, 24U, 0U},
    {264U, 438U, 184U, 32U, 24U, 0U},
    {352U, 550U, 96U, 24U, 24U, 0U},
    {264U, 592U, 184U, 32U, 0U, 2U},
    {32U, 634U, 416U, 16U, 0U, 1U},
    {240U, 682U, 208U, 32U, 0U, 2U},
    {104U, 730U, 112U, 16U, 0U, 1U},
    {336U, 730U, 112U, 16U, 0U, 1U},
    {32U, 504U, 184U, 16U, 0U, 1U},
    {264U, 504U, 184U, 16U, 0U, 1U}
};

typedef struct {
    const uint16_t *chinese;
    char ascii[40];
    uint16_t color;
    uint8_t valid;
} FieldValue;

static FieldValue s_cache[FIELD_COUNT];
static uint32_t s_last_refresh;
static uint8_t s_next_field;
static uint8_t s_ready;
/* These are available in Keil for checking redraw time on the actual board. */
volatile uint32_t g_lcd_max_field_ms;
volatile uint32_t g_lcd_field_updates;

static void DrawCard(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t header_height) {
    LCD_FillRect(x, y, w, h, COLOR_CARD);
    if (header_height != 0U) {
        LCD_FillRect((uint16_t)(x + 2U), (uint16_t)(y + 2U),
                     (uint16_t)(w - 4U), header_height, COLOR_CARD_HEADER);
    }
    LCD_FillRect(x, y, w, 2U, COLOR_BORDER);
    LCD_FillRect(x, (uint16_t)(y + h - 2U), w, 2U, COLOR_BORDER);
    LCD_FillRect(x, y, 2U, h, COLOR_BORDER);
    LCD_FillRect((uint16_t)(x + w - 2U), y, 2U, h, COLOR_BORDER);
}

static uint8_t IsVerified(const WifiDisplaySnapshot *frame) {
    const WifiTestStatus *s = &frame->status;
    return (uint8_t)(s->at_ok && s->wifi_ok && s->session_ok && s->mqtt_ok &&
                     s->host_verified && !frame->run_rx_errors &&
                     !frame->run_parser_errors && s->stage == 5U);
}

static const uint16_t *GetMessage(const WifiDisplaySnapshot *frame) {
    const WifiTestStatus *s = &frame->status;
    if (frame->run_rx_errors || frame->run_parser_errors ||
        s->last_failure == WIFI_FAILURE_SERIAL)
        return MSG_SERIAL_FAIL;
    if (s->stage == 99U)
        return s->last_failure == WIFI_FAILURE_AT ? MSG_AT_FAIL : MSG_JOIN_FAIL;
    if (IsVerified(frame)) return MSG_PASS;
    if (s->mqtt_busy) return MSG_MQTT;
    switch (s->stage) {
        case 0U: return MSG_BOOT;
        case 1U: return MSG_DETECT;
        case 2U: return MSG_JOIN;
        case 3U: return MSG_WAIT;
        case 4U: return MSG_MQTT;
        default:
            if (s->mqtt_attempted && !s->mqtt_ok) return MSG_HOST;
            return MSG_WAIT;
    }
}

static void GetField(DisplayField field, const WifiDisplaySnapshot *frame,
                     uint32_t now, FieldValue *value) {
    const WifiTestStatus *s = &frame->status;
    const uint8_t bad_serial =
        (uint8_t)(frame->run_rx_errors || frame->run_parser_errors);
    (void)memset(value, 0, sizeof(*value));
    value->valid = 1U;
    value->color = COLOR_MUTED;
    switch (field) {
        case FIELD_STATE:
            value->color = COLOR_WARNING;
            if (s->stage == 99U || bad_serial) {
                value->chinese = s->stage == 99U ? STR_RETRY : STR_ABNORMAL;
                value->color = COLOR_ERROR;
            } else if (IsVerified(frame)) {
                value->chinese = STR_PASS;
                value->color = COLOR_GOOD;
            } else if (s->stage == 0U) value->chinese = STR_BOOT;
            else if (s->stage == 1U) value->chinese = STR_DETECT;
            else if (s->stage == 2U) value->chinese = STR_CONNECT;
            else if (s->stage == 3U || s->stage == 4U ||
                     s->mqtt_busy) value->chinese = STR_TEST;
            else if (!s->wifi_ok || !s->mqtt_ok) {
                value->chinese = STR_ABNORMAL;
                value->color = COLOR_ERROR;
            } else value->chinese = STR_WAIT;
            break;
        case FIELD_MESSAGE:
            value->chinese = GetMessage(frame);
            if (IsVerified(frame)) value->color = COLOR_GOOD;
            else if (s->stage == 99U || bad_serial) value->color = COLOR_ERROR;
            break;
        case FIELD_WIFI:
            value->chinese = STR_NOT_CONNECTED;
            if (s->wifi_ok) {
                value->chinese = STR_CONNECTED;
                value->color = COLOR_GOOD;
            } else if (s->stage == 1U) {
                value->chinese = STR_DETECTING;
                value->color = COLOR_WARNING;
            } else if (s->stage == 2U) {
                value->chinese = STR_CONNECTING;
                value->color = COLOR_WARNING;
            }
            break;
        case FIELD_IP:
            (void)snprintf(value->ascii, sizeof(value->ascii), "%s",
                           s->wifi_ok && frame->ip[0] ? frame->ip : "--");
            value->color = COLOR_VALUE;
            break;
        case FIELD_SESSION:
            value->chinese = STR_NOT_STARTED;
            if (s->session_busy) {
                value->chinese = STR_TESTING;
                value->color = COLOR_WARNING;
            } else if (s->session_ok && s->wifi_ok && s->mqtt_ok) {
                value->chinese = STR_PASSED;
                value->color = COLOR_GOOD;
            } else if (s->mqtt_ok) {
                value->chinese = STR_WAIT_VERIFY;
                value->color = COLOR_WARNING;
            }
            break;
        case FIELD_MQTT:
            value->chinese = STR_NOT_STARTED;
            if (s->mqtt_busy) {
                value->chinese = STR_CONNECTING;
                value->color = COLOR_WARNING;
            } else if (s->mqtt_ok && s->wifi_ok) {
                value->chinese = STR_CONNECTED;
                value->color = COLOR_GOOD;
            } else if (s->mqtt_attempted) {
                value->chinese = STR_WAIT_RETRY;
                value->color = COLOR_ERROR;
            }
            break;
        case FIELD_VERIFY:
            value->chinese = IsVerified(frame) ? STR_PASSED : STR_WAIT_VERIFY;
            value->color = IsVerified(frame) ? COLOR_GOOD : COLOR_WARNING;
            break;
        case FIELD_ECHO:
            (void)snprintf(value->ascii, sizeof(value->ascii), "%lu",
                           (unsigned long)s->mqtt_echoes);
            value->color = COLOR_VALUE;
            break;
        case FIELD_AT:
            if (s->at_ok) {
                (void)snprintf(value->ascii, sizeof(value->ascii),
                               "AT: %lu baud  /  USART2 PA2-PA3",
                               (unsigned long)s->at_baud);
            } else (void)strcpy(value->ascii, "AT: --  /  USART2 PA2-PA3");
            break;
        case FIELD_UPTIME: {
            const uint32_t seconds = now / 1000U;
            (void)snprintf(value->ascii, sizeof(value->ascii),
                           "%02lu:%02lu:%02lu",
                           (unsigned long)(seconds / 3600U),
                           (unsigned long)((seconds / 60U) % 60U),
                           (unsigned long)(seconds % 60U));
            value->color = COLOR_TEXT;
            break;
        }
        case FIELD_UART_ERRORS:
            (void)snprintf(value->ascii, sizeof(value->ascii), "%lu",
                           (unsigned long)frame->run_rx_errors);
            value->color = frame->run_rx_errors ? COLOR_ERROR : COLOR_GOOD;
            break;
        case FIELD_PARSE_ERRORS:
            (void)snprintf(value->ascii, sizeof(value->ascii), "%lu",
                           (unsigned long)frame->run_parser_errors);
            value->color = frame->run_parser_errors ? COLOR_ERROR : COLOR_GOOD;
            break;
        case FIELD_SERVER:
            (void)snprintf(value->ascii, sizeof(value->ascii), "HOST %s",
                           s->wifi_ok && frame->server[0] ? frame->server : "--");
            break;
        case FIELD_SEQ:
            (void)snprintf(value->ascii, sizeof(value->ascii), "SEQ %lu",
                           (unsigned long)s->seq);
            break;
        default: break;
    }
}

static void DrawField(DisplayField field, const FieldValue *value) {
    const FieldGeometry *g = &s_geometry[field];
    const uint32_t start = Board_Millis();
    uint32_t elapsed;
    /* Erase the full value area so shorter text cannot leave old pixels. */
    LCD_FillRect(g->x, g->y, g->w, g->h, COLOR_CARD);
    if (value->chinese != NULL) {
        LCD_DrawChineseStringSized(g->x, g->y, value->chinese,
                                   g->chinese_size, value->color, COLOR_CARD);
    } else {
        LCD_DrawStringScaled(g->x, g->y, value->ascii, g->ascii_scale,
                             value->color, COLOR_CARD);
    }
    s_cache[field] = *value;
    elapsed = Board_Millis() - start;
    if (elapsed > g_lcd_max_field_ms) g_lcd_max_field_ms = elapsed;
    ++g_lcd_field_updates;
}

void LCD_Display_Init(void) {
    WifiDisplaySnapshot initial = {0};
    FieldValue value;
    LCD_Init();
    LCD_Clear(COLOR_BG);

    LCD_FillRect(0U, 0U, LCD_WIDTH, 100U, COLOR_HEADER);
    LCD_DrawChineseStringSized(144U, 16U, STR_TITLE, 48U,
                               LCD_COLOR_WHITE, COLOR_HEADER);
    LCD_DrawString(124U, 76U, "STM32F407 / WEB1 / LED TEST",
                   COLOR_HEADER_SUB, COLOR_HEADER);

    DrawCard(16U, 116U, 448U, 104U, 0U);
    LCD_DrawChineseStringSized(32U, 132U, STR_TEST_STATE, 24U,
                               COLOR_TEXT, COLOR_CARD);

    DrawCard(16U, 232U, 448U, 144U, 36U);
    LCD_DrawChineseStringSized(32U, 240U, STR_NETWORK, 24U,
                               COLOR_TEXT, COLOR_CARD_HEADER);
    LCD_DrawStringScaled(368U, 236U, "Wi-Fi", 2U, COLOR_TEXT, COLOR_CARD_HEADER);
    LCD_DrawStringScaled(32U, 282U, WIFI_SSID, 2U, COLOR_TEXT, COLOR_CARD);
    LCD_DrawStringScaled(32U, 328U, "IP", 2U, COLOR_MUTED, COLOR_CARD);

    DrawCard(16U, 388U, 216U, 140U, 36U);
    DrawCard(248U, 388U, 216U, 140U, 36U);
    LCD_DrawStringScaled(32U, 394U, "SESSION", 2U, COLOR_TEXT, COLOR_CARD_HEADER);
    LCD_DrawStringScaled(264U, 394U, "MQTT", 2U, COLOR_TEXT, COLOR_CARD_HEADER);
    LCD_DrawString(32U, 480U, "GET / 200 + token", COLOR_MUTED, COLOR_CARD);
    LCD_DrawString(264U, 480U, "PUB / SUB", COLOR_MUTED, COLOR_CARD);

    DrawCard(16U, 540U, 448U, 120U, 0U);
    LCD_DrawChineseStringSized(32U, 550U, STR_HOST_CHECK, 24U,
                               COLOR_TEXT, COLOR_CARD);
    LCD_DrawChineseStringSized(32U, 594U, STR_ROUND_TRIP, 24U,
                               COLOR_MUTED, COLOR_CARD);

    DrawCard(16U, 672U, 448U, 96U, 0U);
    LCD_DrawChineseStringSized(32U, 682U, STR_RUN_INFO, 24U,
                               COLOR_TEXT, COLOR_CARD);
    LCD_DrawChineseStringSized(32U, 730U, STR_UART_ERROR, 16U,
                               COLOR_MUTED, COLOR_CARD);
    LCD_DrawChineseStringSized(264U, 730U, STR_PARSER_ERROR, 16U,
                               COLOR_MUTED, COLOR_CARD);

    LCD_FillRect(16U, 780U, 24U, 8U, LCD_COLOR_RED);
    LCD_FillRect(44U, 780U, 24U, 8U, LCD_COLOR_GREEN);
    LCD_FillRect(72U, 780U, 24U, 8U, LCD_COLOR_BLUE);
    LCD_FillRect(100U, 780U, 24U, 8U, LCD_COLOR_WHITE);
    LCD_DrawString(148U, 776U, "480x800 / NT35510", COLOR_MUTED, COLOR_BG);
    for (uint32_t i = 0U; i < FIELD_COUNT; ++i) {
        GetField((DisplayField)i, &initial, Board_Millis(), &value);
        DrawField((DisplayField)i, &value);
    }
    s_next_field = 0U;
    s_last_refresh = Board_Millis();
    s_ready = 1U;
    LCD_SetBacklight(1U);
}

void LCD_Display_Service(const WifiDisplaySnapshot *snapshot, uint32_t now) {
    FieldValue value;
    if (!s_ready || (uint32_t)(now - s_last_refresh) < 25U) return;
    s_last_refresh = now;
    for (uint32_t count = 0U; count < FIELD_COUNT; ++count) {
        const DisplayField field =
            (DisplayField)((s_next_field + count) % FIELD_COUNT);
        GetField(field, snapshot, now, &value);
        if (!s_cache[field].valid || s_cache[field].chinese != value.chinese ||
            s_cache[field].color != value.color ||
            strcmp(s_cache[field].ascii, value.ascii) != 0) {
            DrawField(field, &value);
            s_next_field = (uint8_t)(((uint32_t)field + 1U) % FIELD_COUNT);
            return;
        }
    }
}
