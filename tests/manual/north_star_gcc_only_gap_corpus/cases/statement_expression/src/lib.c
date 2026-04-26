#include "gap.h"

#define CLAMP_0_9(value)                                                       \
    ({                                                                         \
        int local_value = (value);                                             \
        local_value < 0 ? 0 : (local_value > 9 ? 9 : local_value);             \
    })

int clamp_score(int value) {
    return CLAMP_0_9(value) + CLAMP_0_9(value + 20);
}

