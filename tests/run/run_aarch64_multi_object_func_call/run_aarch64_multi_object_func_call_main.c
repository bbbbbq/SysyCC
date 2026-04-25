extern void abort(void);
extern long helper(long a, long b, long c, long d, long e, long f, long g, long h,
                   long i);

int main(void) {
    const long result = helper(1, 2, 3, 4, 5, 6, 7, 8, 9);
    if (result != 45) {
        abort();
    }
    return 0;
}
