#include "gap.h"

static int mix_value(int value) {
    return value * value + 15;
}

int project_score(int base) {
    int adjusted = base + 2;
    return mix_value(adjusted) - 32;
}
