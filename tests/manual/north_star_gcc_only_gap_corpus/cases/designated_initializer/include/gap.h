#ifndef SYSYCC_GAP_DESIGNATED_INITIALIZER_H
#define SYSYCC_GAP_DESIGNATED_INITIALIZER_H

struct config {
    int scale;
    int offset;
};

int apply_config(int value);

#endif

