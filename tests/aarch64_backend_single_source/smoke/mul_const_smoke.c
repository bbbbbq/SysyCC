static long mul2(long x) {
    return x * 2;
}

static long mul3(long x) {
    return x * 3;
}

static long mul4(long x) {
    return x * 4;
}

static long mul5(long x) {
    return x * 5;
}

static long mul7(long x) {
    return x * 7;
}

int main(void) {
    volatile long neg = -11;
    volatile long pos = 13;
    volatile long zero = 0;

    if (mul2(neg) != -22) return 1;
    if (mul2(pos) != 26) return 2;
    if (mul2(zero) != 0) return 3;

    if (mul3(neg) != -33) return 4;
    if (mul3(pos) != 39) return 5;
    if (mul3(zero) != 0) return 6;

    if (mul4(neg) != -44) return 7;
    if (mul4(pos) != 52) return 8;
    if (mul4(zero) != 0) return 9;

  if (mul5(neg) != -55) return 10;
    if (mul5(pos) != 65) return 11;
    if (mul5(zero) != 0) return 12;

    if (mul7(neg) != -77) return 13;
    if (mul7(pos) != 91) return 14;
    if (mul7(zero) != 0) return 15;

    return 0;
}
