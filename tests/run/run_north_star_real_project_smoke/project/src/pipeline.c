#include "northstar.h"

int ns_pipeline_score(const int *values, size_t count) {
    int local[16] = {0};
    struct ns_stats stats;
    struct ns_graph graph;
    uint32_t hash = 0;
    int buffered_sum = 0;
    int position = 0;
    int reachable = 0;

    hash = ns_hash_bytes("northstar");
    buffered_sum = ns_buffered_sum(values, count);

    if (count > 16 || buffered_sum < 0) {
        return -1;
    }

    ns_copy_ints(local, values, count);
    ns_sort_ints(local, count);
    stats = ns_stats_make(local, count);
    position = ns_binary_search(local, count, 5);

    ns_graph_init(&graph, 6);
    ns_graph_add_edge(&graph, 0, 2);
    ns_graph_add_edge(&graph, 2, 4);
    ns_graph_add_edge(&graph, 4, 5);
    reachable = ns_graph_reachable(&graph, 0, 5);

    return stats.sum + stats.min + stats.max + position + reachable +
           (hash != 0u ? 11 : 0) + buffered_sum;
}
