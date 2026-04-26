#include "gap.h"

static int scale_value(int value) {
    return value * 3;
}

int project_score(int base) {
    int local = base + 5;
    return scale_value(local) - 3;
}
