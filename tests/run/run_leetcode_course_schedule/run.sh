#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

# This case is a tier1 runtime-semantic guard; O1 loop optimizations are tracked
# separately because the current optimizer can hang on this CFG shape.
export SYSYCC_RUN_COMPILER_FLAGS="${SYSYCC_RUN_COMPILER_FLAGS:--O0}"
exec "${PROJECT_ROOT}/tests/run/run_leetcode_generic.sh" "${SCRIPT_DIR}"
