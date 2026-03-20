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

void starttime(void) {}

void stoptime(void) {}
