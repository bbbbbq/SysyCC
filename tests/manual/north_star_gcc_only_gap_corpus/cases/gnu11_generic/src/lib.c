#include "gap.h"

#define ABS_VALUE(value)                                                       \
    _Generic((value), int: abs_int, long: abs_long)(value)

static int abs_int(int value) {
    return value < 0 ? -value : value;
}

static long abs_long(long value) {
    return value < 0 ? -value : value;
}

int generic_abs_score(void) {
    int small = ABS_VALUE(-7);
    long large = ABS_VALUE(-11L);
    return small + (int)large;
}

