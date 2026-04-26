#include "gap.h"

int apply_config(int value) {
    struct config cfg = {.offset = 7, .scale = 5};
    return value * cfg.scale + cfg.offset;
}

