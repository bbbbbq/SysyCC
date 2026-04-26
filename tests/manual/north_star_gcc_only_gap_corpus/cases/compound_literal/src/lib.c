#include "gap.h"

static int sum_pair(const struct pair *value) {
    return value->left + value->right;
}

int pair_score(void) {
    return sum_pair(&(struct pair){.left = 11, .right = 31});
}

