#include <stddef.h>
#include <string.h>

int main(void) {
    char src[16];
    char dst[16];
    char overlap[16];

    void *(*copy_fn)(void *, const void *, size_t) = memcpy;
    void *(*move_fn)(void *, const void *, size_t) = memmove;
    int (*cmp_fn)(const void *, const void *, size_t) = memcmp;
    size_t (*len_fn)(const char *) = strlen;
    char *(*chr_fn)(const char *, int) = strchr;

    src[0] = 'm';
    src[1] = 'a';
    src[2] = 't';
    src[3] = 'r';
    src[4] = 'i';
    src[5] = 'x';
    src[6] = 0;

    overlap[0] = 'a';
    overlap[1] = 'b';
    overlap[2] = 'c';
    overlap[3] = 'd';
    overlap[4] = 'e';
    overlap[5] = 'f';
    overlap[6] = 'g';
    overlap[7] = 0;

    for (int i = 0; i < 16; ++i) {
        dst[i] = 0;
    }

    copy_fn(dst, src, 7);
    move_fn(overlap + 2, overlap, 5);

    if (cmp_fn(dst, src, 7) != 0) {
        return 101;
    }

    size_t len = len_fn(dst);
    char *hit = chr_fn(dst, 'r');
    if (!hit) {
        return 102;
    }

    return (int)((len * 10 + (size_t)(hit - dst) + (unsigned char)overlap[4]) & 255u);
}
