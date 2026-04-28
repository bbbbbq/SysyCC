#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=parse "${SCRIPT_DIR}/pragma_operator_diagnostic.sy" >/dev/null

echo "verified: _Pragma diagnostic operators from macros are ignored"
