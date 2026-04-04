#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
IMAGE_TAG="sysycc-compiler2025:ubuntu24"
DOCKERFILE_PATH="${PROJECT_ROOT}/Dockerfile"
BUILD_IMAGE=1

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2025/run_functional_in_docker.sh [--image-tag tag] [--no-build] [run_functional.sh args...]

Examples:
  ./tests/compiler2025/run_functional_in_docker.sh
  ./tests/compiler2025/run_functional_in_docker.sh --suite all
  ./tests/compiler2025/run_functional_in_docker.sh --suite h_functional 00_comment2
  ./tests/compiler2025/run_functional_in_docker.sh --no-build --suite functional
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

if [[ "${#SCRIPT_ARGS[@]}" -eq 0 ]]; then
    SCRIPT_ARGS=(--suite all)
fi

if [[ "${BUILD_IMAGE}" -eq 1 ]]; then
    docker build -f "${DOCKERFILE_PATH}" -t "${IMAGE_TAG}" "${PROJECT_ROOT}"
fi

quoted_args=()
for arg in "${SCRIPT_ARGS[@]}"; do
    quoted_args+=("$(printf '%q' "${arg}")")
done

docker run --rm \
    -v "${PROJECT_ROOT}:/workspace" \
    -w /workspace \
    -e SYSYCC_COMPILER2025_BUILD_DIR=/workspace/build-docker \
    -e SYSYCC_TEST_FORCE_CONFIGURE=1 \
    "${IMAGE_TAG}" \
    bash -lc "./tests/compiler2025/run_functional.sh ${quoted_args[*]}"
