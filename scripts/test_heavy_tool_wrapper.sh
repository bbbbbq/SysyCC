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
argv0_name="${SYSYCC_TEST_WRAPPED_ARGV0:-}"
lock_root="${SYSYCC_TEST_HEAVY_TOOL_LOCK_ROOT:-${TMPDIR:-/tmp}/sysycc-heavy-tool-slots}"
wait_announced=0
slot_dir=""
child_pid=""

mkdir -p "${lock_root}"

cleanup() {
    if [[ -n "${child_pid}" ]]; then
        kill "${child_pid}" 2>/dev/null || true
        wait "${child_pid}" 2>/dev/null || true
    fi
    if [[ -n "${slot_dir}" ]]; then
        rm -rf "${slot_dir}"
    fi
}

prune_stale_slots() {
    local candidate_dir

    for candidate_dir in "${lock_root}"/slot.*; do
        if [[ ! -d "${candidate_dir}" ]]; then
            continue
        fi

        owner_pid="$(cat "${candidate_dir}/pid" 2>/dev/null || true)"
        if [[ -z "${owner_pid}" ]]; then
            rm -rf "${candidate_dir}"
            continue
        fi

        if ! kill -0 "${owner_pid}" 2>/dev/null; then
            rm -rf "${candidate_dir}"
        fi
    done
}

trap cleanup EXIT HUP INT TERM

while [[ -z "${slot_dir}" ]]; do
    prune_stale_slots

    for slot in $(seq 1 "${max_jobs}"); do
        local_dir="${lock_root}/slot.${slot}"
        if mkdir "${local_dir}" 2>/dev/null; then
            printf '%s\n' "${BASHPID:-$$}" >"${local_dir}/pid"
            slot_dir="${local_dir}"
            break
        fi

        owner_pid="$(cat "${local_dir}/pid" 2>/dev/null || true)"
        if [[ -z "${owner_pid}" ]]; then
            rm -rf "${local_dir}"
            continue
        fi
        if ! kill -0 "${owner_pid}" 2>/dev/null; then
            rm -rf "${local_dir}"
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

if [[ -n "${argv0_name}" ]]; then
    bash -c 'exec -a "$1" "$2" "${@:3}"' _ "${argv0_name}" "${tool_path}" "$@" &
else
    "${tool_path}" "$@" &
fi

child_pid="$!"
wait "${child_pid}"
status="$?"
child_pid=""
exit "${status}"
