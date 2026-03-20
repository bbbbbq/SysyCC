#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SOURCE_FILE="${PROJECT_ROOT}/src/frontend/preprocess/detail/preprocess_session.cpp"

if grep -Fq 'not a conditional directive' "${SOURCE_FILE}"; then
    echo "error: conditional dispatch still depends on sentinel strings" >&2
    exit 1
fi

if grep -Fq 'not an include directive' "${SOURCE_FILE}"; then
    echo "error: include dispatch still depends on sentinel strings" >&2
    exit 1
fi

if grep -Fq 'not a macro directive' "${SOURCE_FILE}"; then
    echo "error: macro dispatch still depends on sentinel strings" >&2
    exit 1
fi

echo "verified: process_line no longer uses string-sentinel dispatch"
