#include "northstar.h"

uint32_t ns_hash_bytes(const char *text) {
    uint32_t hash = 2166136261u;
    size_t index = 0;

    for (index = 0; text[index] != '\0'; ++index) {
        hash ^= (uint32_t)(unsigned char)text[index];
        hash *= 16777619u;
    }

    return hash;
}
