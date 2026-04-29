struct entry {
    int nr;
    int offset;
};

struct table {
    int nr;
    struct entry entries[64];
};

struct pool {
    int value;
};

struct thread_data {
    int offset;
    int blocks;
    struct table *table;
    struct pool *pool;
};

int sink;

int estimate(int nr) {
    return nr * 3;
}

void init_pool(struct pool *pool, int value) {
    sink = sink + pool->value + value;
}

int sum_thread_blocks(struct thread_data *data, int threads) {
    int i;
    int offset;
    int blocks;
    int start;
    int total;

    offset = 0;
    start = 0;
    total = 0;
    blocks = (data->table->nr + threads - 1) / threads;
    for (i = 0; i < threads; i = i + 1) {
        struct thread_data *thread = &data[i];
        int nr;
        int j;

        if (start + blocks > data->table->nr) {
            blocks = data->table->nr - start;
        }

        thread->offset = offset;
        thread->blocks = blocks;
        nr = 0;
        for (j = thread->offset; j < thread->offset + thread->blocks; j = j + 1) {
            nr = nr + thread->table->entries[j].nr;
        }
        init_pool(thread->pool, estimate(nr));

        for (j = 0; j < blocks; j = j + 1) {
            offset = offset + data->table->entries[start + j].nr;
        }
        start = start + blocks;
        total = total + nr;
    }

    return total + offset;
}

int main(void) {
    return 0;
}
