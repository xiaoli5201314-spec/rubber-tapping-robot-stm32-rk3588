#include "motor_profile.h"
#include "test_support.h"

TEST(emergency_stop_is_immediate_brake)
{
    MotorProfile profile = {0};
    MotorOutput output = MotorProfile_Update(&profile, 300, 0U, 0U);

    ASSERT_EQ_U32(MOTOR_DIR_FORWARD, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);

    output = MotorProfile_Update(&profile, -300, 10U, 1U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);

    output = MotorProfile_Update(&profile, -300, 20U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_REVERSE, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);
}

TEST(forward_command_ramps_by_10_permille)
{
    MotorProfile profile = {0};
    MotorOutput output = MotorProfile_Update(&profile, 300, 0U, 0U);

    ASSERT_EQ_U32(MOTOR_DIR_FORWARD, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);

    output = MotorProfile_Update(&profile, 300, 10U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_FORWARD, output.direction);
    ASSERT_EQ_U32(20U, output.pwm_permille);
}

TEST(output_never_exceeds_300_permille)
{
    MotorProfile profile = {0};
    MotorOutput output = {0};
    uint32_t now_ms;

    for (now_ms = 0U; now_ms <= 400U; now_ms += 10U) {
        output = MotorProfile_Update(&profile, 1000, now_ms, 0U);
        ASSERT_EQ_U32(MOTOR_DIR_FORWARD, output.direction);
        if (output.pwm_permille > 300U) {
            test_failures++;
            return;
        }
    }

    ASSERT_EQ_U32(300U, output.pwm_permille);
}

TEST(reverse_request_first_ramps_to_zero)
{
    MotorProfile profile = {0};
    MotorOutput output = MotorProfile_Update(&profile, 300, 0U, 0U);

    ASSERT_EQ_U32(MOTOR_DIR_FORWARD, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);

    output = MotorProfile_Update(&profile, -300, 10U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);
}

TEST(direction_does_not_change_before_50ms_brake)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, 0U, 0U);
    (void)MotorProfile_Update(&profile, -300, 10U, 0U);
    output = MotorProfile_Update(&profile, -300, 59U, 0U);

    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);
}

TEST(direction_changes_after_brake_interval)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, 0U, 0U);
    (void)MotorProfile_Update(&profile, -300, 10U, 0U);
    output = MotorProfile_Update(&profile, -300, 60U, 0U);

    ASSERT_EQ_U32(MOTOR_DIR_REVERSE, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);
}

TEST(brake_interval_handles_timestamp_wrap)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, UINT32_MAX - 20U, 0U);
    (void)MotorProfile_Update(&profile, -300, UINT32_MAX - 10U, 0U);
    output = MotorProfile_Update(&profile, -300, 39U, 0U);

    ASSERT_EQ_U32(MOTOR_DIR_REVERSE, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);
}

TEST(zero_target_finishes_in_brake_state)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, 0U, 0U);
    output = MotorProfile_Update(&profile, 0, 10U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);

    output = MotorProfile_Update(&profile, 0, 20U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);
}

TEST(neutral_during_reversal_does_not_cancel_50ms_hold)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, 0U, 0U);
    (void)MotorProfile_Update(&profile, -300, 10U, 0U);
    (void)MotorProfile_Update(&profile, 0, 20U, 0U);
    output = MotorProfile_Update(&profile, -300, 30U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);
    output = MotorProfile_Update(&profile, -300, 60U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_REVERSE, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);
}

TEST(neutral_stop_also_holds_before_reverse)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, 0U, 0U);
    (void)MotorProfile_Update(&profile, 0, 10U, 0U);
    output = MotorProfile_Update(&profile, -300, 20U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);
    output = MotorProfile_Update(&profile, -300, 60U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_REVERSE, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);
}

TEST(command_changed_during_hold_uses_current_direction)
{
    MotorProfile profile = {0};
    MotorOutput output;

    (void)MotorProfile_Update(&profile, 300, 0U, 0U);
    (void)MotorProfile_Update(&profile, -300, 10U, 0U);
    (void)MotorProfile_Update(&profile, 0, 20U, 0U);
    output = MotorProfile_Update(&profile, 300, 59U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_BRAKE, output.direction);
    ASSERT_EQ_U32(0U, output.pwm_permille);
    output = MotorProfile_Update(&profile, 300, 60U, 0U);
    ASSERT_EQ_U32(MOTOR_DIR_FORWARD, output.direction);
    ASSERT_EQ_U32(10U, output.pwm_permille);
}

int main(void)
{
    RUN_TEST(emergency_stop_is_immediate_brake);
    RUN_TEST(forward_command_ramps_by_10_permille);
    RUN_TEST(output_never_exceeds_300_permille);
    RUN_TEST(reverse_request_first_ramps_to_zero);
    RUN_TEST(direction_does_not_change_before_50ms_brake);
    RUN_TEST(direction_changes_after_brake_interval);
    RUN_TEST(brake_interval_handles_timestamp_wrap);
    RUN_TEST(zero_target_finishes_in_brake_state);
    RUN_TEST(neutral_during_reversal_does_not_cancel_50ms_hold);
    RUN_TEST(neutral_stop_also_holds_before_reverse);
    RUN_TEST(command_changed_during_hold_uses_current_direction);

    return (test_failures == 0) ? 0 : 1;
}
