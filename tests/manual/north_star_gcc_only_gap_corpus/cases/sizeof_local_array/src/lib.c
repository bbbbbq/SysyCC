#include "gap.h"

size_t local_array_count(void) {
    int values[8];
    return sizeof(values) / sizeof(values[0]);
}

