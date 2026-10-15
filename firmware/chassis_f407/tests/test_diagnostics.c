#include <stdio.h>
#include <string.h>

#include "diagnostics.h"
#include "test_support.h"
#include "usart.h"

UART_HandleTypeDef huart1;

static const uint8_t *transmitted_source;
static uint16_t transmitted_source_length;

HAL_UART_StateTypeDef HAL_UART_GetState(const UART_HandleTypeDef *uart)
{
    return uart->gState;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                      const uint8_t *data,
                                      uint16_t length)
{
    if (uart->gState != HAL_UART_STATE_READY) {
        return HAL_BUSY;
    }
    transmitted_source = data;
    transmitted_source_length = length;
    uart->gState = HAL_UART_STATE_BUSY_TX;
    return HAL_OK;
}

static DiagnosticsFrame MakeFrame(void)
{
    DiagnosticsFrame frame;

    (void)memset(&frame, 0, sizeof(frame));
    frame.time_ms = 123456U;
    frame.rc.ch1_us = 1498U;
    frame.rc.ch2_us = 1800U;
    frame.rc.ch5_us = 1000U;
    frame.rc.ch1_valid = 1U;
    frame.rc.ch2_valid = 1U;
    frame.rc.ch5_valid = 1U;
    frame.rc.ch1_fresh = 1U;
    frame.rc.ch2_fresh = 0U;
    frame.rc.ch5_fresh = 1U;
    frame.safety_state = SAFETY_FAILSAFE;
    frame.left_target_permille = -300;
    frame.right_target_permille = 150;
    frame.left_output.direction = MOTOR_DIR_REVERSE;
    frame.left_output.pwm_permille = 42U;
    frame.right_output.direction = MOTOR_DIR_FORWARD;
    frame.right_output.pwm_permille = 17U;
    return frame;
}

static void AssertStringEquals(const char *expected, const char *actual)
{
    if (strcmp(expected, actual) != 0) {
        (void)fprintf(stderr, "%s:%d: expected <%s>, got <%s>\n",
                      __FILE__, __LINE__, expected, actual);
        test_failures++;
    }
}

TEST(formats_complete_line_with_specific_input_fault)
{
    DiagnosticsFrame frame = MakeFrame();
    char buffer[256];
    uint16_t length;
    const char expected[] =
        "t=123456 ch1=1498 v=1 f=1 ch2=1800 v=1 f=0 "
        "ch5=1000 v=1 f=1 state=FAILSAFE lt=-300 rt=150 "
        "ld=REV lp=42 rd=FWD rp=17 fault=stale-ch2\r\n";

    length = Diagnostics_Format(buffer, (uint16_t)sizeof(buffer), &frame);

    ASSERT_EQ_U32(sizeof(expected) - 1U, length);
    AssertStringEquals(expected, buffer);
}

TEST(bounds_the_formatted_line_and_keeps_it_terminated)
{
    DiagnosticsFrame frame = MakeFrame();
    char buffer[8];
    uint16_t length;

    length = Diagnostics_Format(buffer, (uint16_t)sizeof(buffer), &frame);

    ASSERT_EQ_U32(7U, length);
    ASSERT_EQ_U32(0U, (uint8_t)buffer[7]);
}

TEST(queues_one_line_without_overwriting_a_busy_transmission)
{
    DiagnosticsFrame frame = MakeFrame();
    char first_line[256];
    const uint8_t *first_source;
    uint16_t first_length;

    transmitted_source = NULL;
    transmitted_source_length = 0U;
    huart1.gState = HAL_UART_STATE_READY;
    ASSERT_EQ_U32(1U, Diagnostics_TryTransmit(&frame));
    ASSERT_EQ_U32(1U, transmitted_source != NULL);
    first_source = transmitted_source;
    first_length = transmitted_source_length;
    (void)memcpy(first_line, transmitted_source, (size_t)first_length);
    first_line[first_length] = '\0';

    frame.time_ms = 999999U;
    ASSERT_EQ_U32(0U, Diagnostics_TryTransmit(&frame));
    ASSERT_EQ_U32(1U, transmitted_source == first_source);
    ASSERT_EQ_U32(first_length, transmitted_source_length);
    AssertStringEquals(first_line, (const char *)first_source);

    huart1.gState = HAL_UART_STATE_READY;
    ASSERT_EQ_U32(1U, Diagnostics_TryTransmit(&frame));
    ASSERT_EQ_U32(1U, transmitted_source == first_source);
    ASSERT_EQ_U32(1U, strstr((const char *)transmitted_source,
                            "t=999999 ") == (const char *)transmitted_source);
}

int main(void)
{
    RUN_TEST(formats_complete_line_with_specific_input_fault);
    RUN_TEST(bounds_the_formatted_line_and_keeps_it_terminated);
    RUN_TEST(queues_one_line_without_overwriting_a_busy_transmission);

    if (test_failures != 0) {
        (void)fprintf(stderr, "%d test(s) failed\n", test_failures);
        return 1;
    }
    (void)printf("All diagnostics tests passed\n");
    return 0;
}
