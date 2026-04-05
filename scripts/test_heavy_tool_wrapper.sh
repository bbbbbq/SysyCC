#!/usr/bin/env bash

set -euo pipefail

tool_path="$1"
shift

if [[ "${SYSYCC_TEST_ACTIVE:-0}" != "1" ]]; then
    exec "${tool_path}" "$@"
fi

max_jobs="${SYSYCC_TEST_HEAVY_TOOL_JOBS:-1}"
if [[ ! "${max_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    exec "${tool_path}" "$@"
fi

tool_name="${SYSYCC_TEST_WRAPPED_TOOL_NAME:-$(basename "${tool_path}")}"
lock_root="${SYSYCC_TEST_HEAVY_TOOL_LOCK_ROOT:-${TMPDIR:-/tmp}/sysycc-heavy-tool-slots}"
wait_announced=0
slot_dir=""

mkdir -p "${lock_root}"

cleanup() {
    if [[ -n "${slot_dir}" ]]; then
        rm -rf "${slot_dir}"
    fi
}

trap cleanup EXIT

while [[ -z "${slot_dir}" ]]; do
    for slot in $(seq 1 "${max_jobs}"); do
        local_dir="${lock_root}/slot.${slot}"
        if mkdir "${local_dir}" 2>/dev/null; then
            printf '%s\n' "${BASHPID:-$$}" >"${local_dir}/pid"
            slot_dir="${local_dir}"
            break
        fi

        if owner_pid="$(<"${local_dir}/pid" 2>/dev/null)"; then
            if [[ -n "${owner_pid}" ]] && ! kill -0 "${owner_pid}" 2>/dev/null; then
                rm -rf "${local_dir}"
            fi
        fi
    done

    if [[ -n "${slot_dir}" ]]; then
        break
    fi

    if [[ "${wait_announced}" -eq 0 ]]; then
        echo "==> Waiting for a heavy test tool slot for ${tool_name}" >&2
        wait_announced=1
    fi
    sleep 0.1
done

"${tool_path}" "$@"
