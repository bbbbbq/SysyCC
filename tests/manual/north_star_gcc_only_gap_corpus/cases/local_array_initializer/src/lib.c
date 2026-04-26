#include "gap.h"

int local_table_score(void) {
    int weights[] = {3, 1, 4, 1, 5, 9};
    int total = 0;
    for (int index = 0; index < 6; ++index) {
        total += weights[index];
    }
    return total;
}

