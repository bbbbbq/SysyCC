#include <stdio.h>

int getint(void) {
    int value = 0;
    if (scanf("%d", &value) != 1) {
        return 0;
    }
    return value;
}

void putint(int value) { printf("%d", value); }

void putch(int value) { putchar(value); }

void starttime(void) {}

void stoptime(void) {}
