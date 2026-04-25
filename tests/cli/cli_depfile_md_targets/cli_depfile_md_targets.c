#include "cli_depfile_md_targets.h"
#include <shim_depfile_md_targets.h>

int main(void) {
    return CLI_DEPFILE_MD_TARGETS_LOCAL + CLI_DEPFILE_MD_TARGETS_SYSTEM == 3
               ? 0
               : 1;
}
