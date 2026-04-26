#include "gap.h"

static int affine_value(int value) {
    return value * 2 + 17;
}

int project_score(int base) {
    int adjusted;
    adjusted = base;
    return affine_value(adjusted);
}
