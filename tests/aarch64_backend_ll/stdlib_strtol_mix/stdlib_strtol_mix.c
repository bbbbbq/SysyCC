#include <stdlib.h>

int main(void) {
    char digits[16];
    char extra[16];
    char *end1 = 0;
    char *end2 = 0;

    digits[0] = '1';
    digits[1] = '2';
    digits[2] = '7';
    digits[3] = '\0';

    extra[0] = '-';
    extra[1] = '4';
    extra[2] = '9';
    extra[3] = '\0';

    long a = strtol(digits, &end1, 10);
    long b = strtol(extra, &end2, 10);
    if (!end1 || !end2) {
        return 101;
    }
    if (*end1 != '\0' || *end2 != '\0') {
        return 102;
    }

    long mix = a + (-b) * 3;
    return (int)(mix & 255L);
}
