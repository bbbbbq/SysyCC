extern void abort(void);

typedef long (*op_fn)(long, long);
typedef int v4i32 __attribute__((vector_size(16)));

struct Pair {
    long first;
    long second;
};

extern op_fn select_op(int which);
extern long invoke_twice(op_fn op, long a, long b);
extern struct Pair make_pair(long a, long b);
extern long fold_pair(struct Pair pair);
extern long sum_variadic(int count, ...);
extern v4i32 add_lane_bias(v4i32 input);
extern int reduce_v4i32(v4i32 input);

int main(void) {
    op_fn op = select_op(0);
    if (invoke_twice(op, 3, 4) != 14) {
        abort();
    }
    op = select_op(1);
    if (invoke_twice(op, 3, 4) != 24) {
        abort();
    }

    struct Pair pair = make_pair(5, 7);
    if (pair.first != 15 || pair.second != 27) {
        abort();
    }
    if (fold_pair(pair) != 72) {
        abort();
    }

    if (sum_variadic(5, 1L, 2L, 3L, 4L, 5L) != 15L) {
        abort();
    }
    if (sum_variadic(9, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L) != 45L) {
        abort();
    }

    v4i32 value = {10, 20, 30, 40};
    v4i32 vector_result = add_lane_bias(value);
    if (reduce_v4i32(vector_result) != 110) {
        abort();
    }

    return 0;
}
