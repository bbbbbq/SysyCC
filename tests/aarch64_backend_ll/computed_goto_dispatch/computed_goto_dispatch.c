int dispatch(int x) {
    static void *labels[] = {&&L0, &&L1, &&L2};
    if (x < 0 || x > 2) {
        return -1;
    }
    goto *labels[x];

L0:
    return 11;
L1:
    return 22;
L2:
    return 33;
}

int main(void) {
    int a = dispatch(0);
    int b = dispatch(1);
    int c = dispatch(2);
    return (a == 11 && b == 22 && c == 33) ? 0 : 1;
}
