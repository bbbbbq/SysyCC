#include <stdarg.h>

typedef long (*op_fn)(long, long);
typedef int v4i32 __attribute__((vector_size(16)));

struct Pair {
    long first;
    long second;
};

long add(long a, long b) {
    return a + b;
}

long multiply(long a, long b) {
    return a * b;
}

op_fn select_op(int which) {
    return which ? multiply : add;
}

long invoke_twice(op_fn op, long a, long b) {
    return op(a, b) + op(b, a);
}

struct Pair make_pair(long a, long b) {
    struct Pair pair;
    pair.first = a + 10;
    pair.second = b + 20;
    return pair;
}

long fold_pair(struct Pair pair) {
    return pair.first * 3 + pair.second;
}

long sum_variadic(int count, ...) {
    va_list args;
    va_start(args, count);
    long total = 0;
    for (int i = 0; i < count; ++i) {
        total += va_arg(args, long);
    }
    va_end(args);
    return total;
}

v4i32 add_lane_bias(v4i32 input) {
    v4i32 bias = {1, 2, 3, 4};
    return input + bias;
}

int reduce_v4i32(v4i32 input) {
    return input[0] + input[1] + input[2] + input[3];
}
