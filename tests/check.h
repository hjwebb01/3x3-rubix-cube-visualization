/* Check macro shared by the test files. The counters are defined in test_main.c. */
#ifndef RUBIKS_TESTS_CHECK_H
#define RUBIKS_TESTS_CHECK_H

#include <stdio.h>

extern int g_failures, g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#endif
