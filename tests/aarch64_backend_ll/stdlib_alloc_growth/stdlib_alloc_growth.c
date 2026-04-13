#include <stdlib.h>

int main(void) {
    int *values = (int *)malloc(4 * sizeof(int));
    if (!values) {
        return 101;
    }

    for (int i = 0; i < 4; ++i) {
        values[i] = (i + 1) * (i + 3);
    }

    int *grown = (int *)realloc(values, 8 * sizeof(int));
    if (!grown) {
        free(values);
        return 102;
    }
    values = grown;

    for (int i = 4; i < 8; ++i) {
        values[i] = values[i - 4] + i;
    }

    int total = 0;
    for (int i = 0; i < 8; ++i) {
        total += values[i] * (i + 1);
    }

    free(values);
    return total & 255;
}
