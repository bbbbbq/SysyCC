#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST_FILE="${SCRIPT_DIR}/manifest.txt"
UPSTREAM_DIR="${SCRIPT_DIR}/upstream"

UPSTREAM_URL="$(awk -F': ' '/^Repository:/ { print $2; exit }' "${SCRIPT_DIR}/UPSTREAM_REF.txt")"
UPSTREAM_REF="${1:-$(awk -F': ' '/^Snapshot-commit:/ { print $2; exit }' "${SCRIPT_DIR}/UPSTREAM_REF.txt")}"

if [[ -z "${UPSTREAM_URL}" || -z "${UPSTREAM_REF}" ]]; then
    echo "failed to resolve upstream repo/ref" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

declare -a sparse_paths=()
while IFS='|' read -r expectation c_std source_rel xfail_substring; do
    if [[ -z "${expectation}" || "${expectation}" == \#* ]]; then
        continue
    fi
    sparse_paths+=("${source_rel}")
done <"${MANIFEST_FILE}"

git clone --filter=blob:none --no-checkout --sparse "${UPSTREAM_URL}" "${tmpdir}/llvm-test-suite" >/dev/null 2>&1
cd "${tmpdir}/llvm-test-suite"
git sparse-checkout set --no-cone "${sparse_paths[@]}" >/dev/null 2>&1
git fetch --depth 1 origin "${UPSTREAM_REF}" >/dev/null 2>&1
git checkout FETCH_HEAD >/dev/null 2>&1

mkdir -p "${UPSTREAM_DIR}"
while IFS='|' read -r expectation c_std source_rel xfail_substring; do
    if [[ -z "${expectation}" || "${expectation}" == \#* ]]; then
        continue
    fi
    mkdir -p "${UPSTREAM_DIR}/$(dirname "${source_rel}")"
    cp "${source_rel}" "${UPSTREAM_DIR}/${source_rel}"
done <"${MANIFEST_FILE}"

echo "synced llvm-test-suite SingleSource snapshot ${UPSTREAM_REF}"
