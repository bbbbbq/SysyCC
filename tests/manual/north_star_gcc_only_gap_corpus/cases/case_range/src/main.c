#include "gap.h"

int main(void) {
    return classify_char('7') == 1 && classify_char('q') == 2 &&
                   classify_char('?') == 0
               ? 0
               : 1;
}

