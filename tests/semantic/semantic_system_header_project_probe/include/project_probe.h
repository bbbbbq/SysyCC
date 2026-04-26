#ifndef SYSYCC_PROJECT_PROBE_H
#define SYSYCC_PROJECT_PROBE_H

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

struct ProjectProbeRecord {
    size_t bytes;
    ssize_t signed_bytes;
    off_t offset;
    int32_t score;
    uint64_t mask;
};

int project_probe_fill(struct ProjectProbeRecord *record, const char *src);

#endif
