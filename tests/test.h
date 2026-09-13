#ifndef LAGRANGE_TEST_H
#define LAGRANGE_TEST_H

/*
 * Minimal host unit-test harness (same pattern as starscape's tests/ct_test.h
 * and ClassicNet's cn_test.h): each test executable is a single TU with its own
 * main(), using TEST_CHECK / TEST_CHECK_EQ / TEST_RUN / TEST_SUMMARY.
 */
#include <stdio.h>

static int test_checks_run = 0;
static int test_checks_failed = 0;

#define TEST_CHECK(cond)                                                \
    do {                                                                \
        test_checks_run++;                                             \
        if (!(cond)) {                                                  \
            test_checks_failed++;                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                              \
    } while (0)

#define TEST_CHECK_EQ(actual, expected)                                    \
    do {                                                                   \
        test_checks_run++;                                                 \
        if ((long) (actual) != (long) (expected)) {                        \
            test_checks_failed++;                                          \
            printf("  FAIL %s:%d: %s == %ld, expected %ld\n",             \
                   __FILE__, __LINE__, #actual, (long) (actual),           \
                   (long) (expected));                                     \
        }                                                                  \
    } while (0)

#define TEST_RUN(fn)           \
    do {                       \
        printf("- %s\n", #fn); \
        fn();                  \
    } while (0)

#define TEST_SUMMARY()                                                     \
    (printf("\n%d checks, %d failed\n", test_checks_run,                   \
            test_checks_failed),                                           \
     (test_checks_failed == 0 ? 0 : 1))

#endif /* LAGRANGE_TEST_H */
