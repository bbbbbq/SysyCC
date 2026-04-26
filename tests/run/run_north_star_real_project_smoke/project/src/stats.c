#include "northstar.h"

struct ns_stats ns_stats_make(const int *values, size_t count) {
    struct ns_stats stats;
    size_t index = 0;

    stats.sum = 0;
    stats.min = count == 0 ? 0 : values[0];
    stats.max = count == 0 ? 0 : values[0];

    for (index = 0; index < count; ++index) {
        int value = values[index];
        stats.sum += value;
        if (value < stats.min) {
            stats.min = value;
        }
        if (value > stats.max) {
            stats.max = value;
        }
    }

    return stats;
}
