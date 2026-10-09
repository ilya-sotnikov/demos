#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// https://www.reddit.com/r/C_Programming/comments/vfm3s7/comment/icwsoac/

#define TEST_HEADERS
#include "tests_all.cpp"
#undef TEST_HEADERS

#define TEST(name) test_name = name;

#define TEST_ASSERT(x) \
    do { \
        test_assertion = #x; \
        test_file = __FILE__; \
        test_line = __LINE__; \
        if (x) { \
            putchar('.'); \
        } else { \
            printf( \
                "\ntest failed at %s:%d\n    %s: %s\n", \
                test_file, \
                test_line, \
                test_name, \
                test_assertion \
            ); \
            exit(1); \
        } \
    } while (0)

int main(void) {
    const char* test_name = "";
    const char* test_assertion = "";
    const char* test_file = "";
    int test_line = 0;

#define TEST_SOURCE
#include "tests_all.cpp"
#undef TEST_SOURCE

    printf("\ntests passed\n");

    return 0;
}
