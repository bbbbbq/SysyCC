#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    unsigned char *buf = (unsigned char *)calloc(24, sizeof(unsigned char));
    if (!buf) {
        return 101;
    }

    void *(*set_fn)(void *, int, size_t) = memset;
    void *(*chr_fn)(const void *, int, size_t) = memchr;

    set_fn(buf + 4, 9, 6);
    buf[2] = 3;

    unsigned char *hit = (unsigned char *)chr_fn(buf, 9, 24);
    if (!hit) {
        free(buf);
        return 102;
    }

    int total = 0;
    for (int i = 0; i < 24; ++i) {
        total += buf[i] * (i + 1);
    }
    total += (int)(hit - buf);

    free(buf);
    return total & 255;
}
