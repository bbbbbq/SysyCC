#include "gap.h"

int main(void) {
    return vla_checksum(5) == 15 ? 0 : 1;
}

