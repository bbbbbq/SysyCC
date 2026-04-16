#include "builtin_setjmp_compat.h"

#include <setjmp.h>
#include <stdlib.h>

struct sysycc_builtin_setjmp_entry {
    void **key;
    jmp_buf env;
    int used;
};

static struct sysycc_builtin_setjmp_entry g_sysycc_builtin_setjmp_table[32];

static struct sysycc_builtin_setjmp_entry *
sysycc_lookup_builtin_setjmp_entry(void **key, int create) {
    int index;
    for (index = 0; index < 32; ++index) {
        if (g_sysycc_builtin_setjmp_table[index].used &&
            g_sysycc_builtin_setjmp_table[index].key == key) {
            return &g_sysycc_builtin_setjmp_table[index];
        }
    }
    if (!create) {
        return 0;
    }
    for (index = 0; index < 32; ++index) {
        if (!g_sysycc_builtin_setjmp_table[index].used) {
            g_sysycc_builtin_setjmp_table[index].used = 1;
            g_sysycc_builtin_setjmp_table[index].key = key;
            return &g_sysycc_builtin_setjmp_table[index];
        }
    }
    abort();
}

int sysycc_builtin_setjmp(void **buf) {
    struct sysycc_builtin_setjmp_entry *entry =
        sysycc_lookup_builtin_setjmp_entry(buf, 1);
    return setjmp(entry->env);
}

void sysycc_builtin_longjmp(void **buf, int value) {
    struct sysycc_builtin_setjmp_entry *entry =
        sysycc_lookup_builtin_setjmp_entry(buf, 0);
    if (entry == 0) {
        abort();
    }
    longjmp(entry->env, value ? value : 1);
}
