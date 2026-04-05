#ifndef SYSYCC_TEST_CSMITH_H
#define SYSYCC_TEST_CSMITH_H

/*
 * Minimal local regression stub.
 * This is intentionally not a full upstream Csmith runtime header; it only
 * provides the helper surface needed by the repo-local regression cases.
 */

static short safe_mul_func_int16_t_s_s(short lhs, short rhs) {
    return (short)(lhs * rhs);
}

#endif
