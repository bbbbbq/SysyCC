#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/aarch64_native_object_debug_info.sy"
OBJ_FILE="${CASE_BUILD_DIR}/aarch64_native_object_debug_info.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -g \
    -O0 \
    -c \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"

READELF_TOOL="$(find_aarch64_readelf)"
SECTION_DUMP="$("${READELF_TOOL}" -S "${OBJ_FILE}")"
grep -q '\.debug_info' <<<"${SECTION_DUMP}"
grep -q '\.debug_abbrev' <<<"${SECTION_DUMP}"
grep -q '\.debug_str' <<<"${SECTION_DUMP}"
grep -q '\.rela.debug_info' <<<"${SECTION_DUMP}"

INFO_DUMP="$("${READELF_TOOL}" --debug-dump=info "${OBJ_FILE}")"
grep -q 'DW_TAG_compile_unit' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_subprogram' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_base_type' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_pointer_type' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_array_type' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_subrange_type' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_structure_type' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_member' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_formal_parameter' <<<"${INFO_DUMP}"
grep -q 'DW_TAG_variable' <<<"${INFO_DUMP}"
grep -q 'DW_AT_frame_base' <<<"${INFO_DUMP}"
grep -q 'DW_AT_location' <<<"${INFO_DUMP}"
grep -q 'DW_AT_decl_file' <<<"${INFO_DUMP}"
grep -q 'DW_AT_decl_line' <<<"${INFO_DUMP}"
grep -q 'DW_AT_decl_column' <<<"${INFO_DUMP}"
grep -q 'DW_AT_data_member_location' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*add' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*a$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*b$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*c$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*p$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*values$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*pair$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*field0$' <<<"${INFO_DUMP}"
grep -Eq 'DW_AT_name[[:space:]]+:.*field1$' <<<"${INFO_DUMP}"
grep -q 'DW_OP_fbreg' <<<"${INFO_DUMP}"

echo "verified: native AArch64 -g object emission carries DWARF compile-unit, type, declaration, parameter, and local-variable debug info"
