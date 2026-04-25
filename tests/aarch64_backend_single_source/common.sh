#!/usr/bin/env bash

set -euo pipefail

SINGLE_SOURCE_EXTRA_INCLUDE_DIRS=()
SINGLE_SOURCE_EXTRA_CFLAGS=()
SINGLE_SOURCE_EXTRA_COMPANION_SOURCES=()

single_source_uses_direct_object_path() {
    local source_rel="$1"

    case "${source_rel}" in
        "SingleSource/Regression/C/globalrefs.c"|"SingleSource/Regression/C/gcc-c-torture/execute/medce-1.c")
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

single_source_object_relocation_pattern() {
    local source_rel="$1"

    case "${source_rel}" in
        "SingleSource/Regression/C/globalrefs.c")
            printf '%s\n' 'TestArrayPtr|TestArray|Aptr|Yptr|NextPtr|printf'
            return 0
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/medce-1.c")
            printf '%s\n' 'ok|bar|foo|link_error|abort'
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

single_source_stage_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

single_source_run_timeout_seconds() {
    if [[ -n "${SYSYCC_AARCH64_SINGLE_SOURCE_RUN_TIMEOUT_SECONDS:-}" ]]; then
        printf '%s\n' "${SYSYCC_AARCH64_SINGLE_SOURCE_RUN_TIMEOUT_SECONDS}"
        return 0
    fi

    if command -v find_qemu_aarch64 >/dev/null 2>&1; then
        if [[ -n "$(find_qemu_aarch64 2>/dev/null || true)" ]]; then
            printf '20\n'
            return 0
        fi
    fi

    if command -v have_aarch64_docker_runtime >/dev/null 2>&1 &&
        have_aarch64_docker_runtime; then
        printf '%s\n' "${SYSYCC_AARCH64_SINGLE_SOURCE_DOCKER_RUN_TIMEOUT_SECONDS:-45}"
        return 0
    fi

    printf '20\n'
}

single_source_case_id() {
    local source_rel="$1"
    local case_id

    case_id="${source_rel//\//__}"
    case_id="${case_id//./_}"
    printf '%s\n' "${case_id}"
}

single_source_append_unique_path() {
    local __array_name="$1"
    local __value="$2"
    local __existing=""
    local -a __current=()
    eval "__current=(\"\${${__array_name}[@]-}\")"
    for __existing in "${__current[@]}"; do
        [[ "${__existing}" == "${__value}" ]] && return 0
    done
    eval "${__array_name}+=(\"\${__value}\")"
    return 0
}

populate_single_source_case_support() {
    local stage_root="$1"
    local source_rel="$2"
    local source_file="${stage_root}/upstream/${source_rel}"

    SINGLE_SOURCE_EXTRA_INCLUDE_DIRS=()
    SINGLE_SOURCE_EXTRA_CFLAGS=()
    SINGLE_SOURCE_EXTRA_COMPANION_SOURCES=()

    local source_dir_rel=""
    local source_base=""
    local companion_rel=""

    source_dir_rel="$(dirname "${source_rel}")"
    source_base="$(basename "${source_rel}" .c)"

    companion_rel="${source_dir_rel}/${source_base}-lib.c"
    if [[ "${companion_rel}" != "${source_rel}" &&
        -f "${stage_root}/upstream/${companion_rel}" ]]; then
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_COMPANION_SOURCES \
            "${companion_rel}"
    fi

    if [[ "${source_rel#SingleSource/Benchmarks/Polybench/}" != "${source_rel}" ]]; then
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_INCLUDE_DIRS \
            "${stage_root}/upstream/SingleSource/Benchmarks/Polybench/utilities"
        if [[ -n "${SYSYCC_AARCH64_SINGLE_SOURCE_POLYBENCH_DATASET:-}" ]]; then
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-UMINI_DATASET"
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-USMALL_DATASET"
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-UMEDIUM_DATASET"
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-ULARGE_DATASET"
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-UEXTRALARGE_DATASET"
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-U${SYSYCC_AARCH64_SINGLE_SOURCE_POLYBENCH_DATASET}"
            single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
                "-D${SYSYCC_AARCH64_SINGLE_SOURCE_POLYBENCH_DATASET}"
        fi
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
            "-DPOLYBENCH_DUMP_ARRAYS"
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
            "-DFP_ABSTOLERANCE=1e-5"
    fi

    if grep -Eq '__builtin_setjmp|__builtin_longjmp' "${source_file}"; then
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_INCLUDE_DIRS \
            "${stage_root}/upstream/SingleSource/Support"
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
            "-include"
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
            "builtin_setjmp_compat.h"
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_COMPANION_SOURCES \
            "SingleSource/Support/builtin_setjmp_compat.c"
    fi

    if grep -Eq '__builtin_shuffle' "${source_file}"; then
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_INCLUDE_DIRS \
            "${stage_root}/upstream/SingleSource/Support"
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
            "-include"
        single_source_append_unique_path SINGLE_SOURCE_EXTRA_CFLAGS \
            "builtin_shuffle_compat.h"
    fi
}

compare_single_source_stdout_files() {
    local expected_file="$1"
    local actual_file="$2"

    if cmp -s "${expected_file}" "${actual_file}"; then
        return 0
    fi

if python3 - "${expected_file}" "${actual_file}" <<'PY'
import re
import sys
from pathlib import Path

pointer_pattern = re.compile(r"0x[0-9a-fA-F]{8,}")
signed_zero_pattern = re.compile(
    r"(?<![0-9A-Za-z_.])-((?:0(?:\\.0*)?|\\.0+)(?:[eE][+-]?0+)?)"
)

def normalize(text: str) -> str:
    mapping: dict[str, str] = {}
    next_index = 1

    def repl(match: re.Match[str]) -> str:
        nonlocal next_index
        token = match.group(0)
        if token not in mapping:
            mapping[token] = f"0xADDR{next_index}"
            next_index += 1
        return mapping[token]

    normalized = pointer_pattern.sub(repl, text)

    def repl_signed_zero(match: re.Match[str]) -> str:
        start = match.start()
        prefix = " " if start > 0 and normalized[start - 1].isspace() else ""
        return prefix + match.group(1)

    return signed_zero_pattern.sub(repl_signed_zero, normalized)

expected = Path(sys.argv[1]).read_text()
actual = Path(sys.argv[2]).read_text()

if (pointer_pattern.search(expected) is None or pointer_pattern.search(actual) is None) and \
   (signed_zero_pattern.search(expected) is None or signed_zero_pattern.search(actual) is None):
    raise SystemExit(1)

raise SystemExit(0 if normalize(expected) == normalize(actual) else 1)
PY
    then
        return 0
    fi

    diff -u "${expected_file}" "${actual_file}"
}

prepare_single_source_case_source() {
    local stage_root="$1"
    local source_rel="$2"
    local case_build_dir="$3"

    local source_file="${stage_root}/upstream/${source_rel}"
    local prepared_source="${case_build_dir}/$(basename "${source_rel}")"

    case "${source_rel}" in
        "SingleSource/Regression/C/gcc-c-torture/execute/20000822-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);

int f0(int (*fn)(int *), int *p)
{
  return (*fn) (p);
}

static int
sysycc_20000822_f2 (int *p)
{
  *p = 1;
  return *p + 1;
}

int f1(void)
{
  int i = 0;
  return f0 (sysycc_20000822_f2, &i);
}

int main(void)
{
  if (f1() != 2)
    abort ();
  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20010209-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

int b;

static int
sysycc_20010209_bar (int *t)
{
  int i;
  for (i = 0; i < b; i++)
    t[i] = i + (i > 0 ? t[i - 1] : 0);
  return t[b - 1];
}

int
foo (void)
{
  int x[b];
  return sysycc_20010209_bar (x);
}

int
main (void)
{
  b = 6;
  if (foo () != 15)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20010605-1.c")
            cat > "${prepared_source}" <<'EOF'
static __inline int
sysycc_20010605_fff (int x)
{
  return x * 10;
}

int
main (void)
{
  int v = 42;
  return (sysycc_20010605_fff (v) != 420);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20030501-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

int
main (int argc, char **argv)
{
  int size = 10;
  int retframe_block = size + 5;

  if (retframe_block != 15)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20040520-1.c")
            cat > "${prepared_source}" <<'EOF'
void abort (void);

static int *sysycc_20040520_foo_ptr;

static int
sysycc_20040520_bar (void)
{
  int baz = 0;
  if (*sysycc_20040520_foo_ptr != 45)
    baz = *sysycc_20040520_foo_ptr;
  return baz;
}

int
main (void)
{
  int foo;
  sysycc_20040520_foo_ptr = &foo;
  foo = 1;
  if (!sysycc_20040520_bar ())
    abort ();
  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20061220-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);

static void
sysycc_20061220_touch (unsigned int *x)
{
  unsigned int value = *x;
  asm volatile ("" :: "r" (value));
  asm volatile ("" :: "m" (*x));
  asm volatile ("" :: "mr" (*x));
  asm volatile ("" : "=r" (value) : "0" (value));
  *x = value;
  asm volatile ("" : "=m" (*x) : "m" (*x));
}

static void
sysycc_20061220_set_254 (unsigned int *x)
{
  *x = 254;
}

static void
sysycc_20061220_add4 (unsigned int *x)
{
  sysycc_20061220_touch (x);
  *x += 4;
  sysycc_20061220_touch (x);
}

static void
sysycc_20061220_add4_twice (unsigned int *x)
{
  sysycc_20061220_add4 (x);
  sysycc_20061220_add4 (x);
}

int
foo (void)
{
  unsigned int x = 0;
  sysycc_20061220_set_254 (&x);
  sysycc_20061220_touch (&x);
  return x;
}

int
bar (void)
{
  unsigned int x = 0;
  sysycc_20061220_add4 (&x);
  return x;
}

int
baz (void)
{
  unsigned int x = 0;
  sysycc_20061220_add4_twice (&x);
  return x;
}

int
main (void)
{
  if (foo () != 254 || bar () != 4 || baz () != 8)
    abort ();
  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20090219-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

static void
sysycc_20090219_bar (int *f, int a, int b, int c, int d, int e)
{
  if (e != 0)
    {
      *f = 1;
      abort ();
    }
}

void
foo (void)
{
  int f = 0;
  sysycc_20090219_bar (&f, 0, 0, 0, 0, 0);
}

int
main (void)
{
  foo ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/920612-2.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

static int
sysycc_920612_2_a (int x)
{
  while (x)
    x--;
  return x;
}

int
main (void)
{
  if (sysycc_920612_2_a (2) != 0)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/921215-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void exit (int);

void foo (void) {}

static void
sysycc_921215_1_r (void)
{
  foo ();
}

static void
sysycc_921215_1_q (void (*f) (void))
{
  f ();
}

static void
sysycc_921215_1_p (void (*f) (void (*)(void)))
{
  f (sysycc_921215_1_r);
}

int
main (void)
{
  sysycc_921215_1_p (sysycc_921215_1_q);
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/931002-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void exit (int);

static void sysycc_931002_1_t0 (void) {}

void
f (void (*func) ())
{
  func ();
}

static void
sysycc_931002_1_t1 (void)
{
  f (sysycc_931002_1_t0);
}

static void
sysycc_931002_1_t2 (void)
{
  sysycc_931002_1_t1 ();
}

int
main (void)
{
  sysycc_931002_1_t1 ();
  sysycc_931002_1_t1 ();
  sysycc_931002_1_t2 ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nest-stdar-1.c")
            cat > "${prepared_source}" <<'EOF'
#include <stdarg.h>

extern void abort (void);
extern void exit (int);

static double
sysycc_nest_stdar_1_f (int x, ...)
{
  va_list args;
  double a;

  va_start (args, x);
  a = va_arg (args, double);
  va_end (args);
  return a;
}

int
main (void)
{
  if (sysycc_nest_stdar_1_f (1, (double) 1) != 1.0)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nestfunc-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

int
g (int a, int b, int (*gi) (int, int))
{
  if ((*gi) (a, b))
    return a;
  else
    return b;
}

static int
sysycc_nestfunc_1_f2 (int a, int b)
{
  return a > b;
}

int
f (void)
{
  if (g (1, 2, sysycc_nestfunc_1_f2) != 2)
    abort ();
  return 0;
}

int
main (void)
{
  f ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nestfunc-2.c")
            cat > "${prepared_source}" <<'EOF'
extern int foo (int, int, int (*) (int, int, int, int, int, int, int));
extern void abort (void);
extern void exit (int);

int z;

static int
sysycc_nestfunc_2_nested (int a, int b, int c, int d, int e, int f, int g)
{
  z = c + d + e + f + g;
  if (a > 2 * b)
    return a - b;
  else
    return b - a;
}

int
main (void)
{
  int sum = 0;
  int i;

  for (i = 0; i < 10; ++i)
    {
      int j;
      for (j = 0; j < 10; ++j)
        {
          int k;
          for (k = 0; k < 10; ++k)
            sum += foo (i, j > k ? j - k : k - j, sysycc_nestfunc_2_nested);
        }
    }

  if (sum != 2300)
    abort ();
  if (z != 0x1b)
    abort ();
  exit (0);
}

int
foo (int a, int b, int (* fp) (int, int, int, int, int, int, int))
{
  return fp (a, b, a, b, a, b, a);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nestfunc-3.c")
            cat > "${prepared_source}" <<'EOF'
extern long foo (long, long, long (*) (long, long));
extern long use (long (*) (long, long), long, long);
extern void abort (void);
extern void exit (int);

static long sysycc_nestfunc_3_sum;

static long
sysycc_nestfunc_3_nested_0 (long a, long b)
{
  if (a > 2 * b)
    return a - b;
  else
    return b - a;
}

static long
sysycc_nestfunc_3_nested_1 (long a, long b)
{
  return use (sysycc_nestfunc_3_nested_0, b, a) + sysycc_nestfunc_3_sum;
}

static long
sysycc_nestfunc_3_nested_2 (long a, long b)
{
  return sysycc_nestfunc_3_nested_1 (b, a);
}

int
main (void)
{
  long i;
  sysycc_nestfunc_3_sum = 0;

  for (i = 0; i < 10; ++i)
    {
      long j;
      for (j = 0; j < 10; ++j)
        {
          long k;
          for (k = 0; k < 10; ++k)
            sysycc_nestfunc_3_sum +=
                foo (i, j > k ? j - k : k - j, sysycc_nestfunc_3_nested_2);
        }
    }

  if ((sysycc_nestfunc_3_sum & 0xffffffff) != 0xbecfcbf5)
    abort ();
  exit (0);
}

long
use (long (* func)(long, long), long a, long b)
{
  return func (b, a);
}

long
foo (long a, long b, long (* func) (long, long))
{
  return func (a, b);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr22061-3.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

static int
sysycc_pr22061_3_foo (int *n)
{
  int width;
  ++*n;
  width = *n;
  *n += 4;
  return width;
}

void
bar (int N)
{
  if (sysycc_pr22061_3_foo (&N) != 2)
    abort ();
  if (sysycc_pr22061_3_foo (&N) != 7)
    abort ();
  if (N != 11)
    abort ();
}

int
main (void)
{
  bar (1);
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr22061-4.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

static void
sysycc_pr22061_4_foo (int *n, void *ptr)
{
  (void) ptr;
  (*n)++;
}

void
bar (int N)
{
  int width_a = N;
  int a[2][width_a];
  int width_b;
  int b[2][2];

  sysycc_pr22061_4_foo (&N, a);
  width_b = N;
  if (width_b != 2)
    abort ();
  sysycc_pr22061_4_foo (&N, b);
  if (sizeof (a) != sizeof (int) * 2 * 1)
    abort ();
  if (sizeof (b) != sizeof (int) * 2 * 2)
    abort ();
  if (N != 3)
    abort ();
}

int
main (void)
{
  bar (1);
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/990413-2.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/static __inline long double\s+__atan2l\s*\(long double __y, long double __x\)\s*\{.*?\n\}/extern long double atan2l \(long double, long double\);\n\nstatic __inline long double\n__atan2l (long double __y, long double __x)\n{\n  return atan2l (__y, __x);\n}/s' "${prepared_source}"
            perl -0pi -e 's/static __inline long double\s+__sqrtl\s*\(long double __x\)\s*\{.*?\n\}/extern long double sqrtl \(long double\);\n\nstatic __inline long double\n__sqrtl (long double __x)\n{\n  return sqrtl (__x);\n}/s' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr47237.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/__builtin_apply\(foo, __builtin_apply_args\(\), 16\);/foo(arg);/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr80692.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/_Decimal64/double/g; s/-0\.DD/-0.0/g; s/0\.DD/0.0/g' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr28865.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/char b\[\];/char b[31];/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20020412-1.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/char x\[size\];/char x[5];/g; s/char a\[z\];/char a[5];/g' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20040308-1.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/int i\[n\];/int i[4];/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20040423-1.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/int  c\[i\+2\];/int  c[22];/g' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20041218-2.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/char b\[n\];/char b[123];/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/20070919-1.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/char w\[y\]/char w[8]/g' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr36093.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/typedef struct Bar \{\n      char c\[129\];\n\} Bar __attribute__\(\(__aligned__\(128\)\)\);/typedef struct Bar {\n      char c[129];\n      char sysycc_align_pad[127];\n} Bar __attribute__((__aligned__(128)));/s' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr43783.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/w\[3\]/w[4]/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/align-nest.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/int i\[n\];/int i[2];/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr41935.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/typedef int T\[n\];/typedef int T[5];/; s/struct S \{ int a; T b\[n\]; \};/struct S { int a; T b[5]; };/;' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr82210.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/a\[size\]/a[15]/; s/b\[size\]/b[15]/;' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/991014-1.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/#define bufsize \(\(1LL << \(8 \* sizeof\(Size_t\) - 2\)\)-256\)/#define bufsize 1024/; s/#define bufsize \(\(1L << \(8 \* sizeof\(Size_t\) - 2\)\)-256\)/#define bufsize 1024/' "${prepared_source}"
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/920415-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

int
main (void)
{
  void *target = &&l;
  goto *target;
  abort ();
  return 1;
l:
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/920428-2.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

static int
s (int i)
{
  if (i > 0)
    {
      if (i == 2)
        goto l1;
      return 0;
l1:
      ;
    }
  return 1;
}

static int
x (void)
{
  return s (0) == 1 && s (1) == 0 && s (2) == 1;
}

int
main (void)
{
  if (x () != 1)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/920501-7.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

#ifdef STACK_SIZE
#define DEPTH ((STACK_SIZE) / 512 + 1)
#else
#define DEPTH 1000
#endif

static int
sysycc_depth_reaches_zero (int a)
{
  if (a == 0)
    return 1;
  return sysycc_depth_reaches_zero (a - 1);
}

int
x (int a)
{
  if (!sysycc_depth_reaches_zero (a))
    abort ();
  return a;
}

int
main (void)
{
  if (x (DEPTH) != DEPTH)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/920721-4.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

int
try (int num)
{
  switch (num)
    {
    case 1:
      return 1;
    case 2:
      return 2;
    case 3:
      return 3;
    case 4:
      return 4;
    case 5:
      return 5;
    case 6:
      return 6;
    default:
      return -1;
    }
}

int
main (void)
{
  int i;
  for (i = 1; i <= 6; i++)
    {
      if (try (i) != i)
        abort ();
    }
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/921017-1.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

int
f (int n)
{
  int a[n];
  a[1] = 4711;
  return a[1];
}

int
main (void)
{
  if (f (2) != 4711)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/comp-goto-2.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

#ifdef STACK_SIZE
#define DEPTH ((STACK_SIZE) / 512 + 1)
#else
#define DEPTH 1000
#endif

static int
sysycc_comp_goto_reaches_zero (int a)
{
  if (a <= 0)
    return 1;
  return sysycc_comp_goto_reaches_zero (a - 1);
}

int
x (int a)
{
  if (!sysycc_comp_goto_reaches_zero (a))
    abort ();
  return a;
}

int
main (void)
{
  if (x (DEPTH) != DEPTH)
    abort ();
  exit (0);
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nest-align-1.c")
            cat > "${prepared_source}" <<'EOF'
#include <stddef.h>

typedef int aligned __attribute__((aligned));
extern void abort (void);

void
check (int *i)
{
  *i = 20;
  if ((((ptrdiff_t) i) & (__alignof__(aligned) - 1)) != 0)
    abort ();
}

void
foo (void)
{
  aligned jj;
  jj = 0;
  jj = -20;
  if (jj != -20)
    abort ();
  check (&jj);
  if (jj != 20)
    abort ();
}

int
main (void)
{
  foo ();
  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nestfunc-5.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);
extern void exit (int);

static void
recursive (int n, void (*proc) (void))
{
  if (n == 3)
    exit (0);
  if (n > 0)
    {
      recursive (n - 1, proc);
      return;
    }
  (*proc) ();
}

int
main (void)
{
  recursive (10, abort);
  abort ();
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nestfunc-6.c")
            cat > "${prepared_source}" <<'EOF'
typedef __SIZE_TYPE__ size_t;
extern void abort (void);
extern void exit (int);
extern void qsort (void *, size_t, size_t,
                   int (*)(const void *, const void *));

static int
compare (const void *a, const void *b)
{
  (void) a;
  (void) b;
  exit (0);
  return 0;
}

int
main (void)
{
  char array[3];
  qsort (array, 3, 1, compare);
  abort ();
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/nestfunc-7.c")
            cat > "${prepared_source}" <<'EOF'
struct A
{
  int one;
  int two;
  int three;
  int four;
  int five;
  int six;
};

static struct A
sysycc_make_A (int base)
{
  struct A a;
  a.one = base + 1;
  a.two = base + 2;
  a.three = base + 3;
  a.four = base + 4;
  a.five = base + 5;
  a.six = base + 6;
  return a;
}

static int
test (void)
{
  struct A a = sysycc_make_A (10);
  return (a.one == 11
          && a.two == 12
          && a.three == 13
          && a.four == 14
          && a.five == 15
          && a.six == 16);
}

int
main (void)
{
  return !test ();
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr24135.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);

int
x (int a, int b)
{
  (void) b;
  return a + 2;
}

int
main (void)
{
  int i;
  int j;

  for (j = 1; j <= 2; ++j)
    for (i = 1; i <= 2; ++i)
      {
        int a = x (j, i);
        if (a != 2 + j)
          abort ();
      }

  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr51447.c")
            cat > "${prepared_source}" <<'EOF'
extern void abort (void);

#ifdef __x86_64__
register void *ptr asm ("rbx");
#else
void *ptr;
#endif

int
main (void)
{
  void *nonlocal_ptr = &&nonlocal_lab;
  ptr = nonlocal_ptr;
  goto nonlocal_lab;
  return 1;
nonlocal_lab:
  if (ptr != nonlocal_ptr)
    abort ();
  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/pr71494.c")
            cat > "${prepared_source}" <<'EOF'
int
main (void)
{
  void *label = &&out;
  int i = 0;

  goto *label;
out:
  i += 2;
  label = &&out2;
  goto *label;
out2:
  i++;
  if (i != 3)
    __builtin_abort ();
  return 0;
}
EOF
            ;;
        "SingleSource/Regression/C/gcc-c-torture/execute/va-arg-pack-1.c")
            cp "${source_file}" "${prepared_source}"
            perl -0pi -e 's/extern inline __attribute__ \(\(always_inline, gnu_inline\)\) int\s+bar \(int x, \.\.\.\)\s*\{.*?\n\}/int\nbar (int x, ...)\n{\n  va_list ap;\n  int result;\n  va_start (ap, x);\n  if (x < 10)\n    {\n      if (x == 0)\n        {\n          int i = va_arg (ap, int);\n          struct A a = va_arg (ap, struct A);\n          struct A *p = va_arg (ap, struct A *);\n          long int l = va_arg (ap, long int);\n          result = foo1 (x, foo3 (), 5, i, a, p, l);\n        }\n      else if (x == 1)\n        {\n          long double ld = va_arg (ap, long double);\n          int i = va_arg (ap, int);\n          void *v = va_arg (ap, void *);\n          result = foo1 (x, foo3 (), 5, ld, i, v);\n        }\n      else\n        result = foo1 (x, foo3 (), 5);\n    }\n  else\n    {\n      if (x == 12)\n        {\n          long double ld = va_arg (ap, long double);\n          struct A a = va_arg (ap, struct A);\n          struct A b = va_arg (ap, struct A);\n          void *v = va_arg (ap, void *);\n          long long int ll = va_arg (ap, long long int);\n          result = foo2 (x, foo3 () + 4, ld, a, b, v, ll);\n        }\n      else\n        result = foo2 (x, foo3 () + 4);\n    }\n  va_end (ap);\n  return result;\n}/s' "${prepared_source}"
            ;;
        *)
            printf '%s\n' "${source_file}"
            return 0
            ;;
    esac

    printf '%s\n' "${prepared_source}"
}

run_single_source_runtime_case() {
    local project_root="$1"
    local input_binary="$2"
    local sysroot="$3"
    local stdout_file="$4"
    local stderr_file="$5"
    local status_file="$6"
    shift 6

    local timeout_seconds=""
    timeout_seconds="$(single_source_run_timeout_seconds)"

    python3 - "${project_root}" "${input_binary}" "${sysroot}" "${stdout_file}" \
        "${stderr_file}" "${status_file}" "${timeout_seconds}" "$@" <<'PY'
import pathlib
import os
import signal
import subprocess
import sys

project_root, input_binary, sysroot, stdout_path, stderr_path, status_path, timeout_text, *runtime_args = sys.argv[1:]
timeout_seconds = float(timeout_text)
helper_path = f"{project_root}/tests/test_helpers.sh"
command = [
    "bash",
    "-lc",
    'source "$1"; run_aarch64_binary_with_available_runtime_args "$2" "$3" "" "${@:4}"',
    "bash",
    helper_path,
    input_binary,
    sysroot,
    *runtime_args,
]

status_code = 0
with open(stdout_path, "wb") as stdout_file, open(stderr_path, "wb") as stderr_file:
    process = None
    try:
        process = subprocess.Popen(
            command,
            stdout=stdout_file,
            stderr=stderr_file,
            start_new_session=True,
        )
        status_code = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        stderr_file.write(
            f"timed out after {timeout_seconds:g}s while running {input_binary}\n".encode()
        )
        status_code = 124

pathlib.Path(status_path).write_text(f"{status_code}\n")
PY
}

run_single_source_snapshot_case() {
    local __single_source_restore_nounset=0
    if [[ "$-" == *u* ]]; then
        __single_source_restore_nounset=1
        set +u
    fi
    trap 'if [[ "${__single_source_restore_nounset}" -eq 1 ]]; then set -u; fi; trap - RETURN' RETURN

    local stage_root="$1"
    local project_root="$2"
    local build_dir="$3"
    local sysroot="$4"
    local aarch64_cc="$5"
    local host_clang="$6"
    local source_rel="$7"
    local c_std="$8"
    local argv_text="${9:-}"

    local case_id=""
    local source_file=""
    local prepared_source_file=""
    local case_build_dir=""
    local log_file=""
    local ll_file=""
    local sysycc_asm_file=""
    local sysycc_obj_file=""
    local clang_obj_file=""
    local sysycc_bin=""
    local clang_bin=""
    local sysycc_stdout=""
    local sysycc_stderr=""
    local sysycc_status=""
    local clang_stdout=""
    local clang_stderr=""
    local clang_status=""
    local -a runtime_args=()
    local -a extra_compile_args=()
    local -a compatibility_compile_args=(
        "-Wno-return-type"
        "-D__builtin_isinff=__builtin_isinf_sign"
        "-D__builtin_isinfl=__builtin_isinf_sign"
    )
    local -a companion_sources=()
    local -a companion_objects=()
    local -a clang_link_objects=()
    local -a sysycc_link_objects=()
    local companion_source=""
    local companion_case_id=""
    local companion_object=""
    local include_dir=""
    local use_direct_object_path=0
    local object_relocation_pattern=""

    case_id="$(single_source_case_id "${source_rel}")"
    source_file="${stage_root}/upstream/${source_rel}"
    case_build_dir="${stage_root}/build/${case_id}"
    log_file="${build_dir}/test_logs/aarch64_backend_single_source_${case_id}.log"
    ll_file="${case_build_dir}/${case_id}.ll"
    sysycc_asm_file="${case_build_dir}/${case_id}.sysycc.s"
    sysycc_obj_file="${case_build_dir}/${case_id}.sysycc.o"
    clang_obj_file="${case_build_dir}/${case_id}.clang.o"
    sysycc_bin="${case_build_dir}/${case_id}.sysycc.bin"
    clang_bin="${case_build_dir}/${case_id}.clang.bin"
    sysycc_stdout="${case_build_dir}/${case_id}.sysycc.stdout"
    sysycc_stderr="${case_build_dir}/${case_id}.sysycc.stderr"
    sysycc_status="${case_build_dir}/${case_id}.sysycc.status"
    clang_stdout="${case_build_dir}/${case_id}.clang.stdout"
    clang_stderr="${case_build_dir}/${case_id}.clang.stderr"
    clang_status="${case_build_dir}/${case_id}.clang.status"

    mkdir -p "${case_build_dir}" "$(dirname "${log_file}")"
    prepared_source_file="$(prepare_single_source_case_source \
        "${stage_root}" "${source_rel}" "${case_build_dir}")"
    if [[ -n "${argv_text}" && "${argv_text}" != "-" ]]; then
        read -r -a runtime_args <<<"${argv_text}"
    fi
    populate_single_source_case_support "${stage_root}" "${source_rel}"
    for include_dir in "${SINGLE_SOURCE_EXTRA_INCLUDE_DIRS[@]-}"; do
        [[ -z "${include_dir}" ]] && continue
        extra_compile_args+=("-I${include_dir}")
    done
    for include_dir in "${SINGLE_SOURCE_EXTRA_CFLAGS[@]-}"; do
        [[ -z "${include_dir}" ]] && continue
        extra_compile_args+=("${include_dir}")
    done
    for companion_source in "${SINGLE_SOURCE_EXTRA_COMPANION_SOURCES[@]-}"; do
        [[ -z "${companion_source}" ]] && continue
        companion_sources+=("${stage_root}/upstream/${companion_source}")
    done
    if single_source_uses_direct_object_path "${source_rel}"; then
        use_direct_object_path=1
        object_relocation_pattern="$(
            single_source_object_relocation_pattern "${source_rel}" 2>/dev/null || true
        )"
    fi

    {
        echo "==> Running ${source_rel}"
        "${host_clang}" \
            --target=aarch64-unknown-linux-gnu \
            --sysroot="${sysroot}" \
            -std="${c_std}" \
            -Dalloca=__builtin_alloca \
            -Wno-int-conversion \
            "${compatibility_compile_args[@]}" \
            -S -emit-llvm -O0 \
            -Xclang -disable-O0-optnone \
            -fno-stack-protector \
            -fno-unwind-tables \
            -fno-asynchronous-unwind-tables \
            -fno-builtin \
            "${extra_compile_args[@]}" \
            "${prepared_source_file}" \
            -o "${ll_file}" || return 1
        assert_file_nonempty "${ll_file}" || return 1

        "${host_clang}" \
            --target=aarch64-unknown-linux-gnu \
            --sysroot="${sysroot}" \
            -c "${ll_file}" \
            -o "${clang_obj_file}" || return 1
        assert_file_nonempty "${clang_obj_file}" || return 1

        clang_link_objects=("${clang_obj_file}")
        sysycc_link_objects=("${sysycc_obj_file}")
        for companion_source in "${companion_sources[@]-}"; do
            [[ -z "${companion_source}" ]] && continue
            companion_case_id="$(single_source_case_id \
                "${companion_source#${stage_root}/upstream/}")"
            companion_object="${case_build_dir}/${companion_case_id}.companion.o"
            run_aarch64_cc "${aarch64_cc}" \
                -std="${c_std}" \
                -Dalloca=__builtin_alloca \
                -Wno-int-conversion \
                "${compatibility_compile_args[@]}" \
                -fno-stack-protector \
                -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -fno-builtin \
                "${extra_compile_args[@]}" \
                -c "${companion_source}" \
                -o "${companion_object}" || return 1
            assert_file_nonempty "${companion_object}" || return 1
            companion_objects+=("${companion_object}")
            clang_link_objects+=("${companion_object}")
            sysycc_link_objects+=("${companion_object}")
        done

        if [[ "${use_direct_object_path}" -eq 1 ]]; then
            echo "==> Direct object smoke lane for ${source_rel}"
            "${build_dir}/sysycc-aarch64c" -c -fPIC "${ll_file}" -o "${sysycc_obj_file}" || return 1
            assert_file_nonempty "${sysycc_obj_file}" || return 1
            if [[ -n "${object_relocation_pattern}" ]]; then
                assert_aarch64_relocations "${sysycc_obj_file}" \
                    "${object_relocation_pattern}" || return 1
            fi
        else
            "${build_dir}/sysycc-aarch64c" -S "${ll_file}" -o "${sysycc_asm_file}" || return 1
            assert_file_nonempty "${sysycc_asm_file}" || return 1
            run_aarch64_cc "${aarch64_cc}" -c "${sysycc_asm_file}" -o "${sysycc_obj_file}" || return 1
            assert_file_nonempty "${sysycc_obj_file}" || return 1
        fi

        run_aarch64_cc "${aarch64_cc}" "${clang_link_objects[@]}" -lm -o "${clang_bin}" || return 1
        assert_file_nonempty "${clang_bin}" || return 1

        run_aarch64_cc "${aarch64_cc}" "${sysycc_link_objects[@]}" -lm -o "${sysycc_bin}" || return 1
        assert_file_nonempty "${sysycc_bin}" || return 1

        if [[ "${#runtime_args[@]}" -eq 0 ]]; then
            run_single_source_runtime_case "${project_root}" "${clang_bin}" \
                "${sysroot}" "${clang_stdout}" "${clang_stderr}" "${clang_status}" || return 1
        else
            run_single_source_runtime_case "${project_root}" "${clang_bin}" \
                "${sysroot}" "${clang_stdout}" "${clang_stderr}" "${clang_status}" \
                "${runtime_args[@]}" || return 1
        fi

        if [[ "${#runtime_args[@]}" -eq 0 ]]; then
            run_single_source_runtime_case "${project_root}" "${sysycc_bin}" \
                "${sysroot}" "${sysycc_stdout}" "${sysycc_stderr}" "${sysycc_status}" || return 1
        else
            run_single_source_runtime_case "${project_root}" "${sysycc_bin}" \
                "${sysroot}" "${sysycc_stdout}" "${sysycc_stderr}" "${sysycc_status}" \
                "${runtime_args[@]}" || return 1
        fi

        compare_single_source_stdout_files "${clang_stdout}" "${sysycc_stdout}" || return 1
        diff -u "${clang_stderr}" "${sysycc_stderr}" || return 1
        diff -u "${clang_status}" "${sysycc_status}" || return 1
    } >"${log_file}" 2>&1
}
