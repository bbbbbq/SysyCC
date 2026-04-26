#include "northstar.h"

#include <stdio.h>

int main(void) {
    int values[6];
    size_t count = 6;
    int score = 0;
    int weighted = 0;

    values[0] = 7;
    values[1] = 1;
    values[2] = 5;
    values[3] = 3;
    values[4] = 9;
    values[5] = 2;

    score = ns_pipeline_score(values, count);
    weighted = ns_weighted_sum(values, count);

    if (score != 79) {
        return 10;
    }
    if (weighted != 279) {
        return 11;
    }

    printf("northstar: score=%d weighted=%d\n", score, weighted);
    return 0;
}
