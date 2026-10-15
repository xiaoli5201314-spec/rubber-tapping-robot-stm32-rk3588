#include "chassis_math.h"
#include "test_support.h"

#define ASSERT_EQ_I16(expected, actual)                                                \
    do {                                                                                \
        int16_t test_expected_ = (int16_t)(expected);                                  \
        int16_t test_actual_ = (int16_t)(actual);                                      \
        if (test_expected_ != test_actual_) {                                          \
            (void)fprintf(stderr, "%s:%d: expected %d, got %d\n", __FILE__,          \
                          __LINE__, (int)test_expected_, (int)test_actual_);           \
            test_failures++;                                                           \
            return;                                                                     \
        }                                                                               \
    } while (0)

TEST(center_is_zero)
{
    ASSERT_EQ_I16(0, ChassisMath_PulseToCommand(1500U, 1500U, 50U, 0U));
}

TEST(deadband_edges_are_zero)
{
    ASSERT_EQ_I16(0, ChassisMath_PulseToCommand(1450U, 1500U, 50U, 0U));
    ASSERT_EQ_I16(0, ChassisMath_PulseToCommand(1550U, 1500U, 50U, 0U));
}

TEST(full_forward_is_1000_1000)
{
    ChassisCommand command = ChassisMath_Mix(1000, 0);
    ASSERT_EQ_I16(1000, command.left);
    ASSERT_EQ_I16(1000, command.right);
}

TEST(full_reverse_is_minus_1000_minus_1000)
{
    ChassisCommand command = ChassisMath_Mix(-1000, 0);
    ASSERT_EQ_I16(-1000, command.left);
    ASSERT_EQ_I16(-1000, command.right);
}

TEST(pure_right_turn_is_1000_minus_1000)
{
    ChassisCommand command = ChassisMath_Mix(0, 1000);
    ASSERT_EQ_I16(1000, command.left);
    ASSERT_EQ_I16(-1000, command.right);
}

TEST(diagonal_input_is_normalized_without_changing_ratio)
{
    ChassisCommand command = ChassisMath_Mix(1000, 500);
    ASSERT_EQ_I16(1000, command.left);
    ASSERT_EQ_I16(333, command.right);
}

TEST(invert_flag_reverses_axis)
{
    ASSERT_EQ_I16(1000, ChassisMath_PulseToCommand(1000U, 1500U, 50U, 1U));
}

TEST(pulse_inputs_are_clamped_before_mapping)
{
    ASSERT_EQ_I16(-1000, ChassisMath_PulseToCommand(0U, 1500U, 50U, 0U));
    ASSERT_EQ_I16(1000, ChassisMath_PulseToCommand(UINT32_MAX, 1500U, 50U, 0U));
}

int main(void)
{
    RUN_TEST(center_is_zero);
    RUN_TEST(deadband_edges_are_zero);
    RUN_TEST(full_forward_is_1000_1000);
    RUN_TEST(full_reverse_is_minus_1000_minus_1000);
    RUN_TEST(pure_right_turn_is_1000_minus_1000);
    RUN_TEST(diagonal_input_is_normalized_without_changing_ratio);
    RUN_TEST(invert_flag_reverses_axis);
    RUN_TEST(pulse_inputs_are_clamped_before_mapping);

    return (test_failures == 0) ? 0 : 1;
}
