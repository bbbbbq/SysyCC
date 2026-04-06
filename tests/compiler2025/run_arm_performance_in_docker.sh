#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
IMAGE_TAG="sysycc-compiler2025:ubuntu24"
DOCKERFILE_PATH="${PROJECT_ROOT}/Dockerfile"
BUILD_IMAGE=1

source "${SCRIPT_DIR}/compiler2025_docker_common.sh"

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2025/run_arm_performance_in_docker.sh [--image-tag tag] [--no-build] [run_arm_performance.sh args...]

Examples:
  ./tests/compiler2025/run_arm_performance_in_docker.sh --case-root /path/to/ARM-性能 --iterations 1 --warmup 0
  ./tests/compiler2025/run_arm_performance_in_docker.sh --no-build --case-root /path/to/ARM-性能 crypto-1
  ./tests/compiler2025/run_arm_performance_in_docker.sh --report /tmp/arm_perf.md --case-root /path/to/ARM-性能
EOF
}

SCRIPT_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
    --image-tag)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --image-tag" >&2
            exit 1
        fi
        IMAGE_TAG="$2"
        shift 2
        ;;
    --no-build)
        BUILD_IMAGE=0
        shift
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        SCRIPT_ARGS+=("$1")
        shift
        ;;
    esac
done

compiler2025_docker_build_image_if_requested \
    "${BUILD_IMAGE}" "${IMAGE_TAG}" "${DOCKERFILE_PATH}" "${PROJECT_ROOT}"
compiler2025_docker_prepare_script_args "${PROJECT_ROOT}" "${SCRIPT_ARGS[@]}"
compiler2025_docker_run_script \
    "${IMAGE_TAG}" "./tests/compiler2025/run_arm_performance.sh"

