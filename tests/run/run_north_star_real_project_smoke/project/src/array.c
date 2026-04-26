#include "northstar.h"

#ifndef NORTHSTAR_SCALE
#error NORTHSTAR_SCALE must be provided by the build system
#endif

void ns_copy_ints(int *dst, const int *src, size_t count) {
    size_t index = 0;
    for (index = 0; index < count; ++index) {
        dst[index] = src[index];
    }
}

int ns_buffered_sum(const int *values, size_t count) {
    int copy[16];
    int total = 0;
    size_t index = 0;

    if (count > 16) {
        return -1;
    }

    ns_copy_ints(copy, values, count);
    for (index = 0; index < count; ++index) {
        total += copy[index];
    }

    return total;
}

int ns_weighted_sum(const int *values, size_t count) {
    int total = 0;
    size_t index = 0;
    for (index = 0; index < count; ++index) {
        total += values[index] * (int)(index + 1) * NORTHSTAR_SCALE;
    }
    return total;
}
