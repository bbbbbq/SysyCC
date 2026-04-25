#include "dep_public.h"
#include "dep_private.h"
#include <dep_system.h>

int dep_add(int a, int b) { return a + b + DEP_PRIVATE_OFFSET + DEP_SYSTEM_OFFSET; }
