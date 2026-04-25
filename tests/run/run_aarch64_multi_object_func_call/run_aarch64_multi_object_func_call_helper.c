long helper(long a, long b, long c, long d, long e, long f, long g, long h,
            long i) {
    volatile long bias = 11;
    return a + b + c + d + e + f + g + h + i + bias - 11;
}
