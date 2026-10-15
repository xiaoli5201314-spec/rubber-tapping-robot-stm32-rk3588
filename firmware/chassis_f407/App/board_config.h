#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Only the separate rc_bench_f407 target enables this. */
#ifndef RC_BENCH_TEST
#define RC_BENCH_TEST                    0
#endif

#define RC_CENTER_US                 1500U
#define RC_DEADBAND_US                 50U
#define RC_TIMEOUT_MS                 100U
#define RC_ARM_THRESHOLD_US          1700U
#define RC_ARM_NEUTRAL_HOLD_MS       1000U
/* Actual-motor targets select 30%, 60% or 100% at build time.
   The established RC simulation and other chassis targets retain 30%. */
#if defined(WEB_MOTOR_TEST) && WEB_MOTOR_TEST
#ifndef WEB_MOTOR_OUTPUT_PERMILLE
#define WEB_MOTOR_OUTPUT_PERMILLE    1000U
#endif
#if WEB_MOTOR_OUTPUT_PERMILLE != 300 && \
    WEB_MOTOR_OUTPUT_PERMILLE != 600 && \
    WEB_MOTOR_OUTPUT_PERMILLE != 1000
#error Select a supported motor comparison limit: 300, 600 or 1000.
#endif
#define MOTOR_MAX_OUTPUT_PERMILLE    WEB_MOTOR_OUTPUT_PERMILLE
#else
#define MOTOR_MAX_OUTPUT_PERMILLE     300U
#endif
#define MOTOR_RAMP_STEP_PER_10MS       10U
#define MOTOR_REVERSE_BRAKE_MS          50U

#define RC_ARM_ACTIVE_HIGH              1U
#define RC_STEERING_INVERT               0U
#define RC_THROTTLE_INVERT               0U
#define MOTOR_LEFT_INVERT                0U
#define MOTOR_RIGHT_INVERT               0U

#endif
