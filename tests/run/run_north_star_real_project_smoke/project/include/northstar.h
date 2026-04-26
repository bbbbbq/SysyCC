#ifndef SYSYCC_NORTH_STAR_H
#define SYSYCC_NORTH_STAR_H

#include <stddef.h>
#include <stdint.h>

struct ns_stats {
    int sum;
    int min;
    int max;
};

struct ns_graph {
    int node_count;
    int edges[8][8];
};

void ns_copy_ints(int *dst, const int *src, size_t count);
int ns_buffered_sum(const int *values, size_t count);
int ns_weighted_sum(const int *values, size_t count);

struct ns_stats ns_stats_make(const int *values, size_t count);
void ns_sort_ints(int *values, size_t count);
int ns_binary_search(const int *values, size_t count, int needle);
uint32_t ns_hash_bytes(const char *text);

void ns_graph_init(struct ns_graph *graph, int node_count);
void ns_graph_add_edge(struct ns_graph *graph, int from, int to);
int ns_graph_reachable(const struct ns_graph *graph, int start, int goal);

int ns_pipeline_score(const int *values, size_t count);

#endif
