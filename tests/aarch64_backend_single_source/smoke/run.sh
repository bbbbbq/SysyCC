#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export SYSYCC_AARCH64_SINGLE_SOURCE_MANIFEST="${STAGE_ROOT}/smoke_manifest.txt"

exec "${STAGE_ROOT}/imported_suite/run.sh"
