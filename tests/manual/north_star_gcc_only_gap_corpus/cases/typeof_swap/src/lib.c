#include "gap.h"

#define SWAP_VALUES(a, b)                                                      \
    do {                                                                       \
        typeof(a) tmp = (a);                                                   \
        (a) = (b);                                                             \
        (b) = tmp;                                                             \
    } while (0)

int sorted_pair_delta(int left, int right) {
    if (left > right) {
        SWAP_VALUES(left, right);
    }
    return right - left;
}

