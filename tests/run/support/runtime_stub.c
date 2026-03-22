#include <math.h>
#include <stdio.h>

int getint(void) {
    int value = 0;
    if (scanf("%d", &value) != 1) {
        return 0;
    }
    return value;
}

float getfloat(void) {
    float value = 0.0F;
    if (scanf("%f", &value) != 1) {
        return 0.0F;
    }
    return value;
}

void putint(int value) { printf("%d", value); }

void putfloat(float value) { printf("%f", value); }

void putch(int value) { putchar(value); }

long double __addtf3(long double lhs, long double rhs) { return lhs + rhs; }

long double __extenddftf2(double value) { return (long double)value; }

long double __floatsitf(int value) { return (long double)value; }

int __fixtfsi(long double value) { return (int)value; }

double __trunctfdf2(long double value) { return (double)value; }

int __unordtf2(long double lhs, long double rhs) {
    return isnan(lhs) || isnan(rhs);
}

int __eqtf2(long double lhs, long double rhs) {
    return lhs == rhs ? 0 : 1;
}

int __getf2(long double lhs, long double rhs) {
    if (lhs > rhs) {
        return 1;
    }
    if (lhs < rhs) {
        return -1;
    }
    return 0;
}

int __lttf2(long double lhs, long double rhs) {
    return lhs < rhs ? -1 : 0;
}

void starttime(void) {}

void stoptime(void) {}
