#include "gap.h"

int vla_checksum(int count) {
    int buffer[count];
    int total = 0;
    for (int index = 0; index < count; ++index) {
        buffer[index] = index + 1;
        total += buffer[index];
    }
    return total;
}

