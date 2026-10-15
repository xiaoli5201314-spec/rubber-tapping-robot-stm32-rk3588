#include "safety.h"
#include "test_support.h"

static SafetyInputs FreshInputs(uint32_t ch1_us, uint32_t ch2_us, uint32_t ch5_us)
{
    SafetyInputs inputs;

    inputs.ch1_fresh = 1U;
    inputs.ch2_fresh = 1U;
    inputs.ch5_fresh = 1U;
    inputs.ch1_us = ch1_us;
    inputs.ch2_us = ch2_us;
    inputs.ch5_us = ch5_us;
    return inputs;
}

static void ArmFromNeutral(SafetyStateMachine *machine, uint32_t start_ms)
{
    SafetyInputs safe_neutral = FreshInputs(1500U, 1500U, 1500U);
    SafetyInputs arm = FreshInputs(1500U, 1500U, 1700U);

    Safety_Update(machine, &safe_neutral, start_ms);
    Safety_Update(machine, &safe_neutral, start_ms + 1000U);
    Safety_Update(machine, &arm, start_ms + 1001U);
}

TEST(power_on_is_locked)
{
    SafetyStateMachine machine;

    Safety_Init(&machine);

    ASSERT_EQ_U32(SAFETY_LOCKED, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(arm_switch_requires_neutral)
{
    SafetyStateMachine machine;
    SafetyInputs moving = FreshInputs(1600U, 1500U, 1700U);

    Safety_Init(&machine);
    Safety_Update(&machine, &moving, 0U);

    ASSERT_EQ_U32(SAFETY_LOCKED, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(neutral_must_hold_for_full_1000ms)
{
    SafetyStateMachine machine;
    SafetyInputs safe_neutral = FreshInputs(1500U, 1500U, 1500U);
    SafetyInputs arm = FreshInputs(1500U, 1500U, 1700U);

    Safety_Init(&machine);
    Safety_Update(&machine, &safe_neutral, 10U);
    Safety_Update(&machine, &safe_neutral, 1009U);
    Safety_Update(&machine, &arm, 1009U);

    ASSERT_EQ_U32(SAFETY_LOCKED, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(early_arm_switch_requires_a_new_disarmed_hold)
{
    SafetyStateMachine machine;
    SafetyInputs safe_neutral = FreshInputs(1500U, 1500U, 1000U);
    SafetyInputs arm = FreshInputs(1500U, 1500U, 2000U);

    Safety_Init(&machine);
    Safety_Update(&machine, &safe_neutral, 10U);
    Safety_Update(&machine, &arm, 210U);
    Safety_Update(&machine, &arm, 5010U);
    ASSERT_EQ_U32(SAFETY_LOCKED, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));

    Safety_Update(&machine, &safe_neutral, 5020U);
    Safety_Update(&machine, &safe_neutral, 6020U);
    Safety_Update(&machine, &arm, 6030U);
    ASSERT_EQ_U32(SAFETY_ARMED, machine.state);
}

TEST(arm_after_neutral_hold)
{
    SafetyStateMachine machine;

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 10U);

    ASSERT_EQ_U32(SAFETY_ARMED, machine.state);
    ASSERT_EQ_U32(1U, Safety_IsDriveAllowed(&machine));
}

TEST(disarm_switch_brakes_immediately)
{
    SafetyStateMachine machine;
    SafetyInputs disarm = FreshInputs(1500U, 1500U, 1699U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    Safety_Update(&machine, &disarm, 1002U);

    ASSERT_EQ_U32(SAFETY_LOCKED, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(stale_channel_enters_failsafe)
{
    SafetyStateMachine machine;
    SafetyInputs stale = FreshInputs(1500U, 1500U, 1700U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    stale.ch2_fresh = 0U;
    Safety_Update(&machine, &stale, 1002U);

    ASSERT_EQ_U32(SAFETY_FAILSAFE, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(invalid_ch1_while_armed_enters_failsafe)
{
    SafetyStateMachine machine;
    SafetyInputs invalid_ch1 = FreshInputs(700U, 1500U, 1700U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    Safety_Update(&machine, &invalid_ch1, 1002U);

    ASSERT_EQ_U32(SAFETY_FAILSAFE, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(invalid_ch2_while_armed_enters_failsafe)
{
    SafetyStateMachine machine;
    SafetyInputs invalid_ch2 = FreshInputs(1500U, 2201U, 1700U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    Safety_Update(&machine, &invalid_ch2, 1002U);

    ASSERT_EQ_U32(SAFETY_FAILSAFE, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(below_range_ch5_while_armed_enters_failsafe)
{
    SafetyStateMachine machine;
    SafetyInputs invalid_ch5 = FreshInputs(1500U, 1500U, 700U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    Safety_Update(&machine, &invalid_ch5, 1002U);

    ASSERT_EQ_U32(SAFETY_FAILSAFE, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(invalid_ch5_does_not_clear_failsafe)
{
    SafetyStateMachine machine;
    SafetyInputs stale = FreshInputs(1500U, 1500U, 1700U);
    SafetyInputs invalid_ch5 = FreshInputs(1500U, 1500U, 700U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    stale.ch1_fresh = 0U;
    Safety_Update(&machine, &stale, 1002U);
    Safety_Update(&machine, &invalid_ch5, 1003U);

    ASSERT_EQ_U32(SAFETY_FAILSAFE, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

TEST(recovery_requires_new_neutral_hold)
{
    SafetyStateMachine machine;
    SafetyInputs stale = FreshInputs(1500U, 1500U, 1700U);
    SafetyInputs safe_neutral = FreshInputs(1500U, 1500U, 1500U);
    SafetyInputs arm = FreshInputs(1500U, 1500U, 1700U);

    Safety_Init(&machine);
    ArmFromNeutral(&machine, 0U);
    stale.ch1_fresh = 0U;
    Safety_Update(&machine, &stale, 1002U);
    Safety_Update(&machine, &safe_neutral, 1010U);
    Safety_Update(&machine, &safe_neutral, 1011U);
    Safety_Update(&machine, &arm, 2010U);

    ASSERT_EQ_U32(SAFETY_LOCKED, machine.state);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));

    Safety_Update(&machine, &safe_neutral, 2011U);
    Safety_Update(&machine, &arm, 2012U);
    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
    Safety_Update(&machine, &safe_neutral, 2020U);
    Safety_Update(&machine, &safe_neutral, 3020U);
    Safety_Update(&machine, &arm, 3021U);
    ASSERT_EQ_U32(SAFETY_ARMED, machine.state);
}

TEST(out_of_range_arm_value_never_arms)
{
    SafetyStateMachine machine;
    SafetyInputs safe_neutral = FreshInputs(1500U, 1500U, 1500U);
    SafetyInputs out_of_range_arm = FreshInputs(1500U, 1500U, UINT32_MAX);

    Safety_Init(&machine);
    Safety_Update(&machine, &safe_neutral, 0U);
    Safety_Update(&machine, &safe_neutral, 1000U);
    Safety_Update(&machine, &out_of_range_arm, 1001U);

    ASSERT_EQ_U32(0U, Safety_IsDriveAllowed(&machine));
}

int main(void)
{
    RUN_TEST(power_on_is_locked);
    RUN_TEST(arm_switch_requires_neutral);
    RUN_TEST(neutral_must_hold_for_full_1000ms);
    RUN_TEST(early_arm_switch_requires_a_new_disarmed_hold);
    RUN_TEST(arm_after_neutral_hold);
    RUN_TEST(disarm_switch_brakes_immediately);
    RUN_TEST(stale_channel_enters_failsafe);
    RUN_TEST(invalid_ch1_while_armed_enters_failsafe);
    RUN_TEST(invalid_ch2_while_armed_enters_failsafe);
    RUN_TEST(below_range_ch5_while_armed_enters_failsafe);
    RUN_TEST(invalid_ch5_does_not_clear_failsafe);
    RUN_TEST(recovery_requires_new_neutral_hold);
    RUN_TEST(out_of_range_arm_value_never_arms);

    return (test_failures == 0) ? 0 : 1;
}
