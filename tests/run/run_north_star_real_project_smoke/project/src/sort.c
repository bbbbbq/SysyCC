#include "northstar.h"

void ns_sort_ints(int *values, size_t count) {
    size_t index = 0;
    for (index = 1; index < count; ++index) {
        int key = values[index];
        size_t cursor = index;

        while (cursor > 0 && values[cursor - 1] > key) {
            values[cursor] = values[cursor - 1];
            --cursor;
        }
        values[cursor] = key;
    }
}
