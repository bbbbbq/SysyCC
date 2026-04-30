#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${CASE_BUILD_DIR}/project"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

cleanup() {
    rm -rf "${CASE_BUILD_DIR}"
}
trap cleanup EXIT

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
cleanup
mkdir -p "${SOURCE_DIR}/include" "${SOURCE_DIR}/src"

cat >"${SOURCE_DIR}/include/project_config.h" <<'EOF'
#define PROJECT_BIAS 5
EOF

cat >"${SOURCE_DIR}/src/alpha.c" <<'EOF'
#include "project_config.h"

int alpha_value(int input) {
    return input + PROJECT_BIAS;
}
EOF

cat >"${SOURCE_DIR}/src/beta.c" <<'EOF'
int beta_value(int input) {
    return input * 2;
}
EOF

cat >"${SOURCE_DIR}/src/main.c" <<'EOF'
int alpha_value(int);
int beta_value(int);

int main(void) {
    return beta_value(alpha_value(16)) == 42 ? 0 : 1;
}
EOF

cat >"${SOURCE_DIR}/configure" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

: "${CC:=cc}"
: "${CPPFLAGS:=}"
: "${CFLAGS:=}"
: "${LDFLAGS:=}"

cat > conftest.c <<'C_EOF'
#ifndef PROJECT_CONFIGURED
#error PROJECT_CONFIGURED must be defined
#endif
int main(void) { return 0; }
C_EOF

"${CC}" ${CPPFLAGS} ${CFLAGS} -E conftest.c -o conftest.i
"${CC}" ${CPPFLAGS} ${CFLAGS} -c conftest.c -o conftest.o
"${CC}" conftest.o ${LDFLAGS} -o conftest
./conftest

cat > config.mk <<CONFIG_EOF
CC := ${CC}
CPPFLAGS := ${CPPFLAGS}
CFLAGS := ${CFLAGS}
LDFLAGS := ${LDFLAGS}
CONFIG_EOF
EOF
chmod +x "${SOURCE_DIR}/configure"

cat >"${SOURCE_DIR}/Makefile" <<'EOF'
include config.mk

SRC_DIR := src

all: app

main.o: $(SRC_DIR)/main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

objects.stamp: $(SRC_DIR)/alpha.c $(SRC_DIR)/beta.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $^
	touch $@

app: main.o objects.stamp
	$(CC) main.o alpha.o beta.o $(LDFLAGS) -o $@

clean:
	rm -f app main.o alpha.o beta.o main.d alpha.d beta.d objects.stamp
EOF

(
    cd "${SOURCE_DIR}"
    CC="${BUILD_DIR}/compiler" \
    CPPFLAGS="-Iinclude -DPROJECT_CONFIGURED=1" \
    CFLAGS="-std=gnu89 -pipe -fstack-protector-strong -fno-builtin-memcmp -Wdate-time -Werror=implicit-function-declaration" \
    LDFLAGS="-Wl,-v" \
    ./configure
    make all
    ./app
)

assert_file_nonempty "${SOURCE_DIR}/main.o"
assert_file_nonempty "${SOURCE_DIR}/alpha.o"
assert_file_nonempty "${SOURCE_DIR}/beta.o"
assert_file_nonempty "${SOURCE_DIR}/main.d"
assert_file_nonempty "${SOURCE_DIR}/alpha.d"
assert_file_nonempty "${SOURCE_DIR}/beta.d"

grep -Fq "main.o:" "${SOURCE_DIR}/main.d"
grep -Fq "alpha.o:" "${SOURCE_DIR}/alpha.d"
grep -Fq "beta.o:" "${SOURCE_DIR}/beta.d"

echo "verified: CC=build/compiler ./configure && make handles gnu89, configure warning/codegen flags, depfiles, and multi-source -c objects"
