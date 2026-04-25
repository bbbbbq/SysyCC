#include "local_dep.h"
#include <system_dep.h>

int matrix_value(void) {
    return LOCAL_DEP_VALUE + SYSTEM_DEP_VALUE;
}
