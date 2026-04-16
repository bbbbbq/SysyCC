struct Pair {
    int a;
    int b;
};

int inc(int x) { return x + 1; }
int twice(int x) { return x * 2; }

int apply(int (*fn)(int), struct Pair *p) {
    int value = fn(p->a + p->b);
    int extra = (p->a < p->b) ? 6 : 4;
    return value + extra;
}

int main(void) {
    struct Pair p;
    p.a = 5;
    p.b = 7;
    int r1 = apply(inc, &p);
    int r2 = apply(twice, &p);
    return (r1 == 19 && r2 == 30) ? 0 : 1;
}
