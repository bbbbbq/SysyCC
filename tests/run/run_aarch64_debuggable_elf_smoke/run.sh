#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/run_aarch64_debuggable_elf_smoke.sy"
OBJ_FILE="${CASE_BUILD_DIR}/run_aarch64_debuggable_elf_smoke.o"
ELF_FILE="${CASE_BUILD_DIR}/run_aarch64_debuggable_elf_smoke.elf"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

AARCH64_CC="$(find_aarch64_cc 2>/dev/null || true)"
SYSROOT="$(find_aarch64_sysroot 2>/dev/null || true)"
READELF_TOOL="$(find_aarch64_readelf 2>/dev/null || true)"
if [[ -z "${AARCH64_CC}" || -z "${SYSROOT}" || -z "${READELF_TOOL}" ]]; then
    echo "skipped: no AArch64 link/debug inspection toolchain available"
    exit 0
fi

mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -g \
    -c \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"
assert_file_nonempty "${OBJ_FILE}"

run_aarch64_cc "${AARCH64_CC}" "${OBJ_FILE}" -o "${ELF_FILE}"
assert_file_nonempty "${ELF_FILE}"

SECTION_DUMP="$("${READELF_TOOL}" -S "${ELF_FILE}")"
grep -q '\.text' <<<"${SECTION_DUMP}"
grep -q '\.eh_frame' <<<"${SECTION_DUMP}"
grep -q '\.debug_line' <<<"${SECTION_DUMP}"
grep -q '\.debug_info' <<<"${SECTION_DUMP}"
grep -q '\.debug_abbrev' <<<"${SECTION_DUMP}"
grep -q '\.debug_str' <<<"${SECTION_DUMP}"
grep -q '\.symtab' <<<"${SECTION_DUMP}"

"${READELF_TOOL}" -s "${ELF_FILE}" | grep -Eq '[[:space:]]FUNC[[:space:]]+GLOBAL[[:space:]]+DEFAULT[[:space:]]+[0-9]+[[:space:]]+main$'
"${READELF_TOOL}" -s "${ELF_FILE}" | grep -Eq '[[:space:]]FUNC[[:space:]]+GLOBAL[[:space:]]+DEFAULT[[:space:]]+[0-9]+[[:space:]]+helper$'
"${READELF_TOOL}" --debug-dump=decodedline "${ELF_FILE}" | \
    grep -Eq 'run_aarch64_debuggable_elf_smoke\.sy'
"${READELF_TOOL}" --debug-dump=decodedline "${ELF_FILE}" | \
    grep -Eq '[[:space:]]3[[:space:]]+0x'
"${READELF_TOOL}" --debug-dump=decodedline "${ELF_FILE}" | \
    grep -Eq '[[:space:]]4[[:space:]]+0x'
"${READELF_TOOL}" --debug-dump=decodedline "${ELF_FILE}" | \
    grep -Eq '[[:space:]]8[[:space:]]+0x'
"${READELF_TOOL}" --debug-dump=info "${ELF_FILE}" | \
    grep -q 'DW_TAG_compile_unit'
"${READELF_TOOL}" --debug-dump=info "${ELF_FILE}" | \
    grep -q 'DW_TAG_subprogram'
"${READELF_TOOL}" --debug-dump=info "${ELF_FILE}" | \
    grep -q 'DW_AT_frame_base'

if have_aarch64_binary_runtime; then
    run_aarch64_binary_with_available_runtime "${ELF_FILE}" "${SYSROOT}" >/dev/null
fi

echo "verified: SysyCC AArch64 -g linked ELF keeps symbols, unwind info, source line tables, and DWARF debug info"
