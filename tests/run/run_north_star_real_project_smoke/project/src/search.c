#include "northstar.h"

int ns_binary_search(const int *values, size_t count, int needle) {
    size_t left = 0;
    size_t right = count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (values[mid] == needle) {
            return (int)mid;
        }
        if (values[mid] < needle) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return -1;
}
