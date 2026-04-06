#!/usr/bin/env bash

compiler2025_docker_resolve_path() {
    local input_path="$1"

    python3 - "${input_path}" <<'PY'
from pathlib import Path
import sys

print(Path(sys.argv[1]).expanduser().resolve(strict=False))
PY
}

compiler2025_docker_rewrite_path_under_workspace() {
    local project_root="$1"
    local host_path="$2"

    python3 - "${project_root}" "${host_path}" <<'PY'
from pathlib import Path
import sys

project_root = Path(sys.argv[1]).resolve()
host_path = Path(sys.argv[2]).resolve(strict=False)

try:
    relative = host_path.relative_to(project_root)
except ValueError:
    sys.exit(1)

print(Path("/workspace") / relative)
PY
}

compiler2025_docker_add_mount_once() {
    local mount_spec="$1"
    local existing_mount=""

    for existing_mount in "${COMPILER2025_DOCKER_EXTRA_MOUNTS[@]-}"; do
        if [[ "${existing_mount}" == "${mount_spec}" ]]; then
            return 0
        fi
    done

    COMPILER2025_DOCKER_EXTRA_MOUNTS+=("${mount_spec}")
}

compiler2025_docker_map_case_root() {
    local project_root="$1"
    local host_case_root="$2"
    local resolved_case_root=""
    local rewritten_path=""

    resolved_case_root="$(compiler2025_docker_resolve_path "${host_case_root}")"
    if rewritten_path="$(
        compiler2025_docker_rewrite_path_under_workspace \
            "${project_root}" "${resolved_case_root}" 2>/dev/null
    )"; then
        COMPILER2025_DOCKER_MAPPED_PATH="${rewritten_path}"
        return 0
    fi

    compiler2025_docker_add_mount_once \
        "${resolved_case_root}:/compiler2025-case-root:ro"
    COMPILER2025_DOCKER_MAPPED_PATH="/compiler2025-case-root"
}

compiler2025_docker_map_report_path() {
    local project_root="$1"
    local host_report_path="$2"
    local resolved_report_path=""
    local rewritten_path=""
    local report_dir=""
    local report_name=""

    resolved_report_path="$(compiler2025_docker_resolve_path "${host_report_path}")"
    if rewritten_path="$(
        compiler2025_docker_rewrite_path_under_workspace \
            "${project_root}" "${resolved_report_path}" 2>/dev/null
    )"; then
        COMPILER2025_DOCKER_MAPPED_PATH="${rewritten_path}"
        return 0
    fi

    report_dir="$(dirname "${resolved_report_path}")"
    report_name="$(basename "${resolved_report_path}")"
    mkdir -p "${report_dir}"
    compiler2025_docker_add_mount_once \
        "${report_dir}:/compiler2025-report-root:rw"
    COMPILER2025_DOCKER_MAPPED_PATH="/compiler2025-report-root/${report_name}"
}

compiler2025_docker_prepare_script_args() {
    local project_root="$1"
    shift

    COMPILER2025_DOCKER_EXTRA_MOUNTS=()
    COMPILER2025_DOCKER_SCRIPT_ARGS=()

    local arg=""
    local mapped_path=""
    while [[ $# -gt 0 ]]; do
        arg="$1"
        shift

        case "${arg}" in
        --case-root)
            if [[ $# -eq 0 ]]; then
                echo "missing value for --case-root" >&2
                return 1
            fi
            compiler2025_docker_map_case_root "${project_root}" "$1"
            COMPILER2025_DOCKER_SCRIPT_ARGS+=(
                "--case-root" "${COMPILER2025_DOCKER_MAPPED_PATH}"
            )
            shift
            ;;
        --case-root=*)
            compiler2025_docker_map_case_root \
                "${project_root}" "${arg#--case-root=}"
            COMPILER2025_DOCKER_SCRIPT_ARGS+=(
                "--case-root=${COMPILER2025_DOCKER_MAPPED_PATH}"
            )
            ;;
        --report)
            if [[ $# -eq 0 ]]; then
                echo "missing value for --report" >&2
                return 1
            fi
            compiler2025_docker_map_report_path "${project_root}" "$1"
            COMPILER2025_DOCKER_SCRIPT_ARGS+=(
                "--report" "${COMPILER2025_DOCKER_MAPPED_PATH}"
            )
            shift
            ;;
        --report=*)
            compiler2025_docker_map_report_path \
                "${project_root}" "${arg#--report=}"
            COMPILER2025_DOCKER_SCRIPT_ARGS+=(
                "--report=${COMPILER2025_DOCKER_MAPPED_PATH}"
            )
            ;;
        *)
            COMPILER2025_DOCKER_SCRIPT_ARGS+=("${arg}")
            ;;
        esac
    done
}

compiler2025_docker_build_image_if_requested() {
    local build_image="$1"
    local image_tag="$2"
    local dockerfile_path="$3"
    local project_root="$4"

    if [[ "${build_image}" -eq 1 ]]; then
        docker build -f "${dockerfile_path}" -t "${image_tag}" "${project_root}"
    fi
}

compiler2025_docker_run_script() {
    local image_tag="$1"
    local script_path="$2"

    local quoted_args=()
    local arg=""
    for arg in "${COMPILER2025_DOCKER_SCRIPT_ARGS[@]-}"; do
        quoted_args+=("$(printf '%q' "${arg}")")
    done

    local docker_cmd=(
        docker run --rm
        -v "${PROJECT_ROOT}:/workspace"
        -w /workspace
        -e SYSYCC_COMPILER2025_BUILD_DIR=/workspace/build-docker
        -e SYSYCC_TEST_FORCE_CONFIGURE=1
        -e SYSYCC_TEST_DISABLE_HOST_TOOL_WRAPPERS=1
    )

    for arg in "${COMPILER2025_DOCKER_EXTRA_MOUNTS[@]-}"; do
        docker_cmd+=(-v "${arg}")
    done

    docker_cmd+=(
        "${image_tag}"
        bash -lc "${script_path} ${quoted_args[*]}"
    )

    "${docker_cmd[@]}"
}
