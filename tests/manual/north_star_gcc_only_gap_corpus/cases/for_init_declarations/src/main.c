#include "gap.h"

int main(void) {
    int values[5] = {1, 2, 3, 4, 5};
    return sum_stride(values, 5, 2) == 9 ? 0 : 1;
}

