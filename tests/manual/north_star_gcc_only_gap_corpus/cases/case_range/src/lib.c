#include "gap.h"

int classify_char(int ch) {
    switch (ch) {
    case '0' ... '9':
        return 1;
    case 'a' ... 'z':
        return 2;
    default:
        return 0;
    }
}

