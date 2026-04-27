#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${1:?usage: refresh_host_compiler_object_compat.sh <build-dir>}"
COMPAT_ROOT="${BUILD_DIR}/CMakeFiles/SysyCC.dir/.sysycc_object_compat"
LEGACY_ROOT="${BUILD_DIR}/CMakeFiles/SysyCC.dir"

rm -rf "${COMPAT_ROOT}"
mkdir -p "${COMPAT_ROOT}"

if [[ -d "${LEGACY_ROOT}" ]]; then
    find "${LEGACY_ROOT}" \
        -path "${COMPAT_ROOT}" -prune -o \
        -name '*.o' ! -name 'main.cpp.o' \
        -delete
fi

TARGET_DIRS=(
    "sysycc_common.dir"
    "sysycc_frontend.dir"
    "sysycc_core_ir.dir"
    "sysycc_compiler_driver.dir"
)

for target_dir in "${TARGET_DIRS[@]}"; do
    SOURCE_ROOT="${BUILD_DIR}/CMakeFiles/${target_dir}"
    [[ -d "${SOURCE_ROOT}" ]] || continue

    while IFS= read -r -d '' object_file; do
        rel_path="${object_file#${BUILD_DIR}/CMakeFiles/}"
        compat_name="${rel_path//\//__}"
        compat_path="${COMPAT_ROOT}/${compat_name}"
        ln -f "${object_file}" "${compat_path}" 2>/dev/null ||
            cp -p "${object_file}" "${compat_path}"
    done < <(find "${SOURCE_ROOT}" -name '*.o' -print0)
done
