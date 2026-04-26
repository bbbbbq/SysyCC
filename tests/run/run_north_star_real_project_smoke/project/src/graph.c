#include "northstar.h"

void ns_graph_init(struct ns_graph *graph, int node_count) {
    int row = 0;
    int col = 0;

    graph->node_count = node_count;
    for (row = 0; row < 8; ++row) {
        for (col = 0; col < 8; ++col) {
            graph->edges[row][col] = 0;
        }
    }
}

void ns_graph_add_edge(struct ns_graph *graph, int from, int to) {
    if (from >= 0 && from < graph->node_count && to >= 0 &&
        to < graph->node_count) {
        graph->edges[from][to] = 1;
    }
}

int ns_graph_reachable(const struct ns_graph *graph, int start, int goal) {
    int seen[8] = {0};
    int queue[8] = {0};
    int head = 0;
    int tail = 0;
    int next = 0;

    if (start < 0 || start >= graph->node_count || goal < 0 ||
        goal >= graph->node_count) {
        return 0;
    }

    seen[start] = 1;
    queue[tail] = start;
    ++tail;

    while (head < tail) {
        int node = queue[head];
        ++head;

        if (node == goal) {
            return 1;
        }

        for (next = 0; next < graph->node_count; ++next) {
            if (graph->edges[node][next] != 0 && seen[next] == 0) {
                seen[next] = 1;
                queue[tail] = next;
                ++tail;
            }
        }
    }

    return 0;
}
