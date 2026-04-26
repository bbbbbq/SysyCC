#include "gap.h"

int sum_stride(const int *values, int count, int stride) {
    int total = 0;
    for (int index = 0; index < count; index += stride) {
        total += values[index];
    }
    return total;
}

