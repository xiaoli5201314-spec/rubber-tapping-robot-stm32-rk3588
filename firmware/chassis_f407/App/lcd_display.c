#include "lcd_display.h"
#include "lcd_driver.h"
#include "board_config.h"

#include <stdio.h>

#define RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                (((uint16_t)(g) & 0xFCU) << 3) | \
                ((uint16_t)(b) >> 3)))

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
#define COLOR_WARNING     RGB565(210, 118, 0)
#define COLOR_ERROR       RGB565(190, 30, 48)

#define HEADER_Y          0U
#define HEADER_H          104U
#define STATUS_X          16U
#define STATUS_Y          120U
#define STATUS_W          448U
#define STATUS_H          112U
#define CHANNEL_X         16U
#define CHANNEL_Y         248U
#define CHANNEL_W         448U
#define CHANNEL_H         196U
#define MOTOR_Y           460U
#define MOTOR_W           216U
#define MOTOR_H           180U
#define LEFT_MOTOR_X      16U
#define RIGHT_MOTOR_X     248U
#define FAULT_X           16U
#define FAULT_Y           656U
#define FAULT_W           448U
#define FAULT_H           104U

static const uint16_t STR_TITLE[] = {
#if RC_BENCH_TEST
    0x9065, 0x63A7, 0x8054, 0x8C03, 0
#else
    0x5E95, 0x76D8, 0x76D1, 0x63A7, 0x7CFB, 0x7EDF, 0
#endif
};
static const uint16_t STR_SYSTEM_STATE[] = {0x7CFB, 0x7EDF, 0x72B6, 0x6001, 0};
static const uint16_t STR_LOCKED[] = {0x9501, 0x5B9A, 0};
static const uint16_t STR_WAIT[] = {0x7B49, 0x5F85, 0};
static const uint16_t STR_ARMED[] = {0x5C31, 0x7EEA, 0};
static const uint16_t STR_FAILSAFE[] = {0x5931, 0x6548, 0};
static const uint16_t STR_CHANNEL_INPUT[] = {0x901A, 0x9053, 0x8F93, 0x5165, 0};
#if RC_BENCH_TEST
static const uint16_t STR_LEFT_MOTOR[] = {0x5DE6, 0x6A21, 0x62DF, 0};
static const uint16_t STR_RIGHT_MOTOR[] = {0x53F3, 0x6A21, 0x62DF, 0};
static const uint16_t STR_MOTOR_DISABLED[] = {0x7535, 0x673A, 0x7981, 0x7528, 0};
#else
static const uint16_t STR_LEFT_MOTOR[] = {0x5DE6, 0x7535, 0x673A, 0};
static const uint16_t STR_RIGHT_MOTOR[] = {0x53F3, 0x7535, 0x673A, 0};
#endif
static const uint16_t STR_TARGET[] = {0x76EE, 0x6807, 0};
#if RC_BENCH_TEST
static const uint16_t STR_OUTPUT[] = {0x6A21, 0x62DF, 0};
#else
static const uint16_t STR_OUTPUT[] = {0x8F93, 0x51FA, 0};
#endif
static const uint16_t STR_FORWARD[] = {0x524D, 0x8FDB, 0};
static const uint16_t STR_REVERSE[] = {0x540E, 0x9000, 0};
static const uint16_t STR_BRAKE[] = {0x5236, 0x52A8, 0};
static const uint16_t STR_FAULT_INFO[] = {0x6545, 0x969C, 0x4FE1, 0x606F, 0};
static const uint16_t STR_NO_SIGNAL[] = {0x901A, 0x9053, 0x65E0, 0x4FE1, 0x53F7, 0};
static const uint16_t STR_INVALID_SIGNAL[] = {
    0x901A, 0x9053, 0x4FE1, 0x53F7, 0x65E0, 0x6548, 0
};
static const uint16_t STR_NO_FAULT[] = {0x65E0, 0x6545, 0x969C, 0};

static void DrawBorder(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color) {
    LCD_FillRect(x, y, w, 2U, color);
    LCD_FillRect(x, (uint16_t)(y + h - 2U), w, 2U, color);
    LCD_FillRect(x, y, 2U, h, color);
    LCD_FillRect((uint16_t)(x + w - 2U), y, 2U, h, color);
}

static void DrawCard(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t header_height) {
    LCD_FillRect(x, y, w, h, COLOR_CARD);
    if (header_height != 0U) {
        LCD_FillRect((uint16_t)(x + 2U), (uint16_t)(y + 2U),
                     (uint16_t)(w - 4U), header_height, COLOR_CARD_HEADER);
    }
    DrawBorder(x, y, w, h, COLOR_BORDER);
}

static const uint16_t *StateToString(SafetyState state) {
    switch (state) {
        case SAFETY_LOCKED:
            return STR_LOCKED;
        case SAFETY_WAIT_NEUTRAL:
            return STR_WAIT;
        case SAFETY_ARMED:
            return STR_ARMED;
        case SAFETY_FAILSAFE:
            return STR_FAILSAFE;
        default:
            return STR_LOCKED;
    }
}

static uint16_t StateColor(SafetyState state) {
    switch (state) {
        case SAFETY_ARMED:
            return COLOR_GOOD;
        case SAFETY_WAIT_NEUTRAL:
            return COLOR_WARNING;
        case SAFETY_FAILSAFE:
            return COLOR_ERROR;
        default:
            return COLOR_WARNING;
    }
}

static const uint16_t *DirectionToString(MotorDirection direction) {
    switch (direction) {
        case MOTOR_DIR_FORWARD:
            return STR_FORWARD;
        case MOTOR_DIR_REVERSE:
            return STR_REVERSE;
        case MOTOR_DIR_BRAKE:
        default:
            return STR_BRAKE;
    }
}

static void DrawPulseValue(uint16_t value_y, uint32_t pulse_us,
                           uint32_t period_us, uint8_t valid, uint8_t fresh) {
    char value[8];
    uint16_t status_color = ((valid != 0U) && (fresh != 0U))
                                ? COLOR_GOOD
                                : COLOR_ERROR;

#if RC_BENCH_TEST
    char period[12];
    if ((valid != 0U) && (fresh != 0U)) {
        (void)snprintf(value, sizeof(value), "%4lu", (unsigned long)pulse_us);
        (void)snprintf(period, sizeof(period), "%lu.%lums",
                       (unsigned long)(period_us / 1000U),
                       (unsigned long)((period_us % 1000U) / 100U));
    } else {
        (void)snprintf(value, sizeof(value), "----");
        (void)snprintf(period, sizeof(period), "--.-ms");
    }
#else
    (void)period_us;
    (void)snprintf(value, sizeof(value), "%4lu", (unsigned long)pulse_us);
#endif
    LCD_FillRect(144U, value_y, 304U, 32U, COLOR_CARD);
    LCD_DrawStringScaled(160U, value_y, value, 2U, COLOR_VALUE, COLOR_CARD);
    LCD_DrawString(250U, (uint16_t)(value_y + 8U), "us", COLOR_MUTED, COLOR_CARD);
#if RC_BENCH_TEST
    LCD_DrawString(280U, (uint16_t)(value_y + 8U), period, COLOR_MUTED, COLOR_CARD);
#endif
    LCD_DrawString(376U, (uint16_t)(value_y + 8U),
                   ((valid != 0U) && (fresh != 0U)) ? "OK" : "--",
                   status_color, COLOR_CARD);
}

static void DrawMotorValues(uint16_t card_x, int16_t target,
                            MotorOutput output) {
    char target_text[8];
    char output_text[8];
    uint16_t value_x = (uint16_t)(card_x + 72U);

    (void)snprintf(target_text, sizeof(target_text), "%+5d", target);
    LCD_FillRect(value_x, 508U, 128U, 32U, COLOR_CARD);
    LCD_DrawStringScaled(value_x, 508U, target_text, 2U, COLOR_VALUE, COLOR_CARD);
#if RC_BENCH_TEST
    LCD_DrawString((uint16_t)(value_x + 80U), 520U, "/1000",
                   COLOR_MUTED, COLOR_CARD);
#endif

    (void)snprintf(output_text, sizeof(output_text), "%3u%%",
                   (unsigned int)(output.pwm_permille / 10U));
    LCD_FillRect(value_x, 552U, 128U, 32U, COLOR_CARD);
    LCD_DrawChineseString(value_x, 560U, DirectionToString(output.direction),
                          COLOR_TEXT, COLOR_CARD);
    LCD_DrawStringScaled((uint16_t)(value_x + 48U), 552U, output_text, 2U,
                         COLOR_VALUE, COLOR_CARD);
}

static void DrawFault(const RcInputSnapshot *rc) {
    const uint16_t *message;
    uint16_t color;
    char detail[32];
    uint8_t any_stale = (uint8_t)((rc->ch1_fresh == 0U) ||
                                  (rc->ch2_fresh == 0U) ||
                                  (rc->ch5_fresh == 0U));
    uint8_t any_invalid = (uint8_t)((rc->ch1_valid == 0U) ||
                                    (rc->ch2_valid == 0U) ||
                                    (rc->ch5_valid == 0U));

    if (any_stale != 0U) {
        message = STR_NO_SIGNAL;
        color = COLOR_ERROR;
    } else if (any_invalid != 0U) {
        message = STR_INVALID_SIGNAL;
        color = COLOR_WARNING;
    } else {
        message = STR_NO_FAULT;
        color = COLOR_GOOD;
    }

    LCD_FillRect(32U, 704U, 416U, 48U, COLOR_CARD);
    LCD_DrawChineseStringSized(32U, 704U, message, 24U, color, COLOR_CARD);

    (void)snprintf(detail, sizeof(detail), "CH1:%s  CH2:%s  CH5:%s",
                   ((rc->ch1_valid != 0U) && (rc->ch1_fresh != 0U)) ? "OK" : "--",
                   ((rc->ch2_valid != 0U) && (rc->ch2_fresh != 0U)) ? "OK" : "--",
                   ((rc->ch5_valid != 0U) && (rc->ch5_fresh != 0U)) ? "OK" : "--");
    LCD_DrawString(32U, 736U, detail, COLOR_MUTED, COLOR_CARD);
}

void LCD_Display_Init(void) {
    LCD_Init();
    LCD_Clear(COLOR_BG);

    LCD_FillRect(0U, HEADER_Y, LCD_WIDTH, HEADER_H, COLOR_HEADER);
#if RC_BENCH_TEST
    LCD_DrawChineseStringSized(144U, 18U, STR_TITLE, 48U,
                               LCD_COLOR_WHITE, COLOR_HEADER);
    LCD_DrawString(140U, 80U, "RC BENCH / MOTORS DISABLED",
                   COLOR_HEADER_SUB, COLOR_HEADER);
#else
    LCD_DrawChineseStringSized(96U, 18U, STR_TITLE, 48U,
                               LCD_COLOR_WHITE, COLOR_HEADER);
    LCD_DrawString(168U, 80U, "STM32F407  CHASSIS", COLOR_HEADER_SUB, COLOR_HEADER);
#endif

    DrawCard(STATUS_X, STATUS_Y, STATUS_W, STATUS_H, 0U);
    LCD_DrawChineseStringSized(40U, 140U, STR_SYSTEM_STATE, 24U,
                               COLOR_TEXT, COLOR_CARD);
#if RC_BENCH_TEST
    LCD_DrawChineseString(40U, 196U, STR_MOTOR_DISABLED, COLOR_ERROR, COLOR_CARD);
    LCD_DrawString(124U, 196U, "SWA: LOW -> CENTER 1s -> HIGH",
                   COLOR_MUTED, COLOR_CARD);
#else
    LCD_DrawString(40U, 196U, "RC SAFE CONTROL", COLOR_MUTED, COLOR_CARD);
#endif

    DrawCard(CHANNEL_X, CHANNEL_Y, CHANNEL_W, CHANNEL_H, 42U);
    LCD_DrawChineseStringSized(32U, 258U, STR_CHANNEL_INPUT, 24U,
                               COLOR_TEXT, COLOR_CARD_HEADER);
#if RC_BENCH_TEST
    LCD_DrawString(352U, 264U, "50Hz / us", COLOR_MUTED, COLOR_CARD_HEADER);
#else
    LCD_DrawString(360U, 264U, "PWM / us", COLOR_MUTED, COLOR_CARD_HEADER);
#endif
    LCD_DrawString(32U, 312U, "CH1", COLOR_TEXT, COLOR_CARD);
    LCD_DrawString(32U, 352U, "CH2", COLOR_TEXT, COLOR_CARD);
    LCD_DrawString(32U, 392U, "CH5", COLOR_TEXT, COLOR_CARD);

    DrawCard(LEFT_MOTOR_X, MOTOR_Y, MOTOR_W, MOTOR_H, 42U);
    DrawCard(RIGHT_MOTOR_X, MOTOR_Y, MOTOR_W, MOTOR_H, 42U);
    LCD_DrawChineseStringSized(32U, 470U, STR_LEFT_MOTOR, 24U,
                               COLOR_TEXT, COLOR_CARD_HEADER);
    LCD_DrawChineseStringSized(264U, 470U, STR_RIGHT_MOTOR, 24U,
                               COLOR_TEXT, COLOR_CARD_HEADER);
    LCD_DrawChineseString(32U, 516U, STR_TARGET, COLOR_MUTED, COLOR_CARD);
    LCD_DrawChineseString(264U, 516U, STR_TARGET, COLOR_MUTED, COLOR_CARD);
    LCD_DrawChineseString(32U, 560U, STR_OUTPUT, COLOR_MUTED, COLOR_CARD);
    LCD_DrawChineseString(264U, 560U, STR_OUTPUT, COLOR_MUTED, COLOR_CARD);
#if RC_BENCH_TEST
    LCD_DrawString(32U, 612U, "SIM / NO DRIVE", COLOR_MUTED, COLOR_CARD);
    LCD_DrawString(264U, 612U, "SIM / NO DRIVE", COLOR_MUTED, COLOR_CARD);
#else
    LCD_DrawString(32U, 612U, "PWM OUTPUT", COLOR_MUTED, COLOR_CARD);
    LCD_DrawString(264U, 612U, "PWM OUTPUT", COLOR_MUTED, COLOR_CARD);
#endif

    DrawCard(FAULT_X, FAULT_Y, FAULT_W, FAULT_H, 38U);
    LCD_DrawChineseStringSized(32U, 664U, STR_FAULT_INFO, 24U,
                               COLOR_TEXT, COLOR_CARD_HEADER);

    LCD_DrawString(108U, 776U, "DISPLAY 480x800  |  UPDATE 200 ms",
                   COLOR_MUTED, COLOR_BG);
    LCD_SetBacklight(1U);
}

void LCD_Display_Update(const DiagnosticsFrame *frame) {
    uint16_t state_color = StateColor(frame->safety_state);

    LCD_FillRect((uint16_t)(STATUS_X + 2U), (uint16_t)(STATUS_Y + 2U),
                 4U, (uint16_t)(STATUS_H - 4U), state_color);
    LCD_FillRect(320U, 136U, 120U, 48U, COLOR_CARD);
    LCD_DrawChineseStringSized(336U, 136U, StateToString(frame->safety_state),
                               48U, state_color, COLOR_CARD);

    DrawPulseValue(304U, frame->rc.ch1_us, frame->rc.ch1_period_us,
                    frame->rc.ch1_valid, frame->rc.ch1_fresh);
    DrawPulseValue(344U, frame->rc.ch2_us, frame->rc.ch2_period_us,
                    frame->rc.ch2_valid, frame->rc.ch2_fresh);
    DrawPulseValue(384U, frame->rc.ch5_us, frame->rc.ch5_period_us,
                    frame->rc.ch5_valid, frame->rc.ch5_fresh);

    DrawMotorValues(LEFT_MOTOR_X, frame->left_target_permille, frame->left_output);
    DrawMotorValues(RIGHT_MOTOR_X, frame->right_target_permille, frame->right_output);
    DrawFault(&frame->rc);
}
