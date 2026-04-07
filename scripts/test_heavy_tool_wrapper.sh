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
child_pgid=""
spawned_dedicated_group=0
wrapper_pgid="$(ps -o pgid= -p "${BASHPID:-$$}" 2>/dev/null | tr -d '[:space:]')"

mkdir -p "${lock_root}"

record_session_process_group() {
    local pgid="$1"
    local pgid_file=""

    if [[ -z "${SYSYCC_TEST_SESSION_ROOT:-}" ]] ||
        [[ -z "${pgid}" ]] ||
        [[ ! "${pgid}" =~ ^[0-9]+$ ]]; then
        return 0
    fi

    pgid_file="${SYSYCC_TEST_SESSION_ROOT}/pgids"
    mkdir -p "${SYSYCC_TEST_SESSION_ROOT}"
    if ! grep -qx "${pgid}" "${pgid_file}" 2>/dev/null; then
        printf '%s\n' "${pgid}" >>"${pgid_file}"
    fi
}

spawn_wrapped_tool() {
    if command -v python3 >/dev/null 2>&1; then
        spawned_dedicated_group=1
        python3 - "${tool_path}" "${argv0_name}" "$@" <<'PY' &
import os
import sys

tool_path = sys.argv[1]
argv0_name = sys.argv[2]
tool_args = sys.argv[3:]

os.setpgid(0, 0)
if argv0_name:
    os.execv(tool_path, [argv0_name, *tool_args])
os.execv(tool_path, [tool_path, *tool_args])
PY
        return 0
    fi

    spawned_dedicated_group=0
    if [[ -n "${argv0_name}" ]]; then
        bash -c 'exec -a "$1" "$2" "${@:3}"' _ "${argv0_name}" "${tool_path}" "$@" &
    else
        "${tool_path}" "$@" &
    fi
}

cleanup() {
    if [[ -n "${child_pgid}" ]]; then
        kill -TERM -- "-${child_pgid}" 2>/dev/null || true
    elif [[ -n "${child_pid}" ]]; then
        kill "${child_pid}" 2>/dev/null || true
    fi

    if [[ -n "${child_pid}" ]]; then
        wait "${child_pid}" 2>/dev/null || true
    fi

    if [[ -n "${child_pgid}" ]]; then
        kill -KILL -- "-${child_pgid}" 2>/dev/null || true
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

spawn_wrapped_tool "$@"
child_pid="$!"
if [[ "${spawned_dedicated_group}" == "1" ]]; then
    child_pgid="${child_pid}"
else
    child_pgid="$(ps -o pgid= -p "${child_pid}" 2>/dev/null | tr -d '[:space:]')"
    if [[ -n "${child_pgid}" ]] && [[ "${child_pgid}" == "${wrapper_pgid}" ]]; then
        child_pgid=""
    fi
fi
record_session_process_group "${child_pgid}"
wait "${child_pid}"
status="$?"
child_pid=""
child_pgid=""
exit "${status}"
