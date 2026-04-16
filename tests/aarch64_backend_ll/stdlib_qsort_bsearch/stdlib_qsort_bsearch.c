#include <stdlib.h>

static int cmp_int_desc(const void *lhs, const void *rhs) {
    int a = *(const int *)lhs;
    int b = *(const int *)rhs;

    if (a < b) {
        return 1;
    }
    if (a > b) {
        return -1;
    }
    return 0;
}

int main(void) {
    int values[8];
    int key = 5;

    values[0] = 4;
    values[1] = 1;
    values[2] = 7;
    values[3] = 2;
    values[4] = 9;
    values[5] = 3;
    values[6] = 5;
    values[7] = 6;

    qsort(values, 8, sizeof(int), cmp_int_desc);
    int *found = (int *)bsearch(&key, values, 8, sizeof(int), cmp_int_desc);
    if (!found) {
        return 101;
    }

    int checksum = 0;
    for (int i = 0; i < 8; ++i) {
        checksum = checksum * 3 + values[i];
    }

    return (checksum + (int)(found - values)) & 255;
}
