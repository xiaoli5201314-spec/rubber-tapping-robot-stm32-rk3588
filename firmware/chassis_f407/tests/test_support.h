#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <stdint.h>
#include <stdio.h>

static int test_failures;

#define TEST(name) static void name(void)

#define ASSERT_EQ_U32(expected, actual)                                                \
    do {                                                                                \
        uint32_t test_expected_ = (uint32_t)(expected);                                \
        uint32_t test_actual_ = (uint32_t)(actual);                                    \
        if (test_expected_ != test_actual_) {                                          \
            (void)fprintf(stderr, "%s:%d: expected %lu, got %lu\n", __FILE__,        \
                          __LINE__, (unsigned long)test_expected_,                     \
                          (unsigned long)test_actual_);                                \
            test_failures++;                                                           \
            return;                                                                     \
        }                                                                               \
    } while (0)

#define RUN_TEST(name)                                                                  \
    do {                                                                                \
        name();                                                                         \
        if (test_failures == 0) {                                                       \
            (void)printf("PASS %s\n", #name);                                        \
        }                                                                               \
    } while (0)

#endif
