#include "lcd_display.h"
#include "lcd_driver.h"
#include "test_support.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t color;
} FillRectCall;

static FillRectCall fill_rect_calls[128];
static size_t fill_rect_call_count;
static uint32_t out_of_bounds_calls;
static uint32_t saw_bench_title;
static uint32_t saw_motor_disabled_label;
static uint32_t saw_simulation_label;
static uint32_t saw_live_pulse;
static uint32_t saw_period;
static uint32_t saw_missing_pulse;
static uint32_t saw_simulated_duty;

static void CheckBounds(uint16_t x, uint16_t y, uint32_t w, uint32_t h) {
    if (((uint32_t)x + w > LCD_WIDTH) ||
        ((uint32_t)y + h > LCD_HEIGHT)) {
        out_of_bounds_calls++;
    }
}

void LCD_Init(void) {}

void LCD_Clear(uint16_t color) {
    (void)color;
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  uint16_t color) {
    CheckBounds(x, y, w, h);
    if (fill_rect_call_count < (sizeof(fill_rect_calls) / sizeof(fill_rect_calls[0]))) {
        FillRectCall *call = &fill_rect_calls[fill_rect_call_count++];
        call->x = x;
        call->y = y;
        call->w = w;
        call->h = h;
        call->color = color;
    }
}

void LCD_DrawString(uint16_t x, uint16_t y, const char *str,
                    uint16_t fg, uint16_t bg) {
    CheckBounds(x, y, (uint32_t)strlen(str) * 8U, 16U);
    if (strcmp(str, "20.0ms") == 0) {
        saw_period++;
    }
    (void)fg;
    (void)bg;
}

void LCD_DrawStringScaled(uint16_t x, uint16_t y, const char *str,
                          uint8_t scale, uint16_t fg, uint16_t bg) {
    CheckBounds(x, y, (uint32_t)strlen(str) * 8U * scale, 16U * scale);
    if (strcmp(str, "1500") == 0) {
        saw_live_pulse++;
    }
    if (strcmp(str, "----") == 0) {
        saw_missing_pulse++;
    }
    if (strcmp(str, " 30%") == 0) {
        saw_simulated_duty++;
    }
    (void)fg;
    (void)bg;
}

void LCD_DrawChineseString(uint16_t x, uint16_t y, const uint16_t *str,
                           uint16_t fg, uint16_t bg) {
    LCD_DrawChineseStringSized(x, y, str, 16U, fg, bg);
}

void LCD_DrawChineseStringSized(uint16_t x, uint16_t y, const uint16_t *str,
                                uint8_t size, uint16_t fg, uint16_t bg) {
    size_t count = 0U;
    const uint16_t title[] = {0x9065, 0x63A7, 0x8054, 0x8C03, 0};
    const uint16_t disabled[] = {0x7535, 0x673A, 0x7981, 0x7528, 0};
    const uint16_t simulated[] = {0x6A21, 0x62DF, 0};
    while (str[count] != 0U) {
        count++;
    }
    CheckBounds(x, y, (uint32_t)count * size, size);
    if ((count == 4U) && (memcmp(str, title, sizeof(title)) == 0)) {
        saw_bench_title++;
    }
    if ((count == 4U) && (memcmp(str, disabled, sizeof(disabled)) == 0)) {
        saw_motor_disabled_label++;
    }
    if ((count == 2U) && (memcmp(str, simulated, sizeof(simulated)) == 0)) {
        saw_simulation_label++;
    }
    (void)fg;
    (void)bg;
}

void LCD_SetBacklight(uint8_t enabled) {
    (void)enabled;
}

TEST(status_strip_stays_inside_status_card_border) {
    DiagnosticsFrame frame = {0};

    fill_rect_call_count = 0U;
    frame.safety_state = SAFETY_LOCKED;
    LCD_Display_Update(&frame);

    ASSERT_EQ_U32(18U, fill_rect_calls[0].x);
    ASSERT_EQ_U32(122U, fill_rect_calls[0].y);
    ASSERT_EQ_U32(4U, fill_rect_calls[0].w);
    ASSERT_EQ_U32(108U, fill_rect_calls[0].h);
}

TEST(all_drawing_fits_portrait_panel) {
    DiagnosticsFrame frame = {0};
    frame.safety_state = SAFETY_ARMED;
    frame.left_target_permille = -300;
    frame.right_target_permille = 300;
    frame.left_output.direction = MOTOR_DIR_REVERSE;
    frame.right_output.direction = MOTOR_DIR_FORWARD;
    frame.left_output.pwm_permille = 300U;
    frame.right_output.pwm_permille = 300U;
    frame.rc.ch1_us = 2000U;
    frame.rc.ch2_us = 1000U;
    frame.rc.ch5_us = 2000U;
    out_of_bounds_calls = 0U;
    LCD_Display_Init();
    LCD_Display_Update(&frame);
    ASSERT_EQ_U32(0U, out_of_bounds_calls);
}

#if defined(RC_BENCH_TEST) && RC_BENCH_TEST
/* Catch a bench image indistinguishable from the physical driving firmware. */
TEST(bench_screen_explicitly_labels_disabled_motors_and_simulation) {
    saw_bench_title = 0U;
    saw_motor_disabled_label = 0U;
    saw_simulation_label = 0U;
    LCD_Display_Init();
    ASSERT_EQ_U32(1U, saw_bench_title);
    ASSERT_EQ_U32(1U, saw_motor_disabled_label);
    ASSERT_EQ_U32(2U, saw_simulation_label);
}

/* Catch dropping live receiver data or substituting it with display constants. */
TEST(bench_screen_shows_live_pulse_period_and_simulated_duty) {
    DiagnosticsFrame frame = {0};
    frame.safety_state = SAFETY_ARMED;
    frame.rc.ch1_us = 1500U;
    frame.rc.ch1_period_us = 20000U;
    frame.rc.ch1_valid = 1U;
    frame.rc.ch1_fresh = 1U;
    frame.left_output.direction = MOTOR_DIR_FORWARD;
    frame.left_output.pwm_permille = 300U;
    saw_live_pulse = 0U;
    saw_period = 0U;
    saw_simulated_duty = 0U;
    LCD_Display_Update(&frame);
    ASSERT_EQ_U32(1U, saw_live_pulse);
    ASSERT_EQ_U32(1U, saw_period);
    ASSERT_EQ_U32(1U, saw_simulated_duty);
}

/* Catch showing stale last-good pulses as if they were a live connection. */
TEST(bench_screen_replaces_stale_pulses_with_dashes) {
    DiagnosticsFrame frame = {0};
    frame.rc.ch1_us = 1500U;
    frame.rc.ch1_valid = 1U;
    frame.rc.ch1_fresh = 0U;
    saw_live_pulse = 0U;
    saw_missing_pulse = 0U;
    LCD_Display_Update(&frame);
    ASSERT_EQ_U32(0U, saw_live_pulse);
    ASSERT_EQ_U32(3U, saw_missing_pulse);
}
#endif

int main(void) {
    RUN_TEST(status_strip_stays_inside_status_card_border);
    RUN_TEST(all_drawing_fits_portrait_panel);
#if defined(RC_BENCH_TEST) && RC_BENCH_TEST
    RUN_TEST(bench_screen_explicitly_labels_disabled_motors_and_simulation);
    RUN_TEST(bench_screen_shows_live_pulse_period_and_simulated_duty);
    RUN_TEST(bench_screen_replaces_stale_pulses_with_dashes);
#endif
    return test_failures;
}
