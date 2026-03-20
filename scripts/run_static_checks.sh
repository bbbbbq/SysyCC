#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"
CLANG_TIDY_CHECKS="${CLANG_TIDY_CHECKS:-clang-analyzer-*,bugprone-*,performance-*}"
EXTRA_CLANG_TIDY_ARGS=()

if [[ "$(uname -s)" == "Darwin" ]]; then
    SDK_PATH="$(xcrun --show-sdk-path)"
    EXTRA_CLANG_TIDY_ARGS+=(--extra-arg=-isysroot)
    EXTRA_CLANG_TIDY_ARGS+=(--extra-arg="${SDK_PATH}")
fi

require_tool() {
    local tool_name="$1"

    if ! command -v "${tool_name}" >/dev/null 2>&1; then
        echo "missing required tool: ${tool_name}" >&2
        return 1
    fi
}

run_clang_tidy() {
    echo "==> clang-tidy"
    local source_files=()
    local source_file
    while IFS= read -r source_file; do
        source_files+=("${source_file}")
    done < <(
        find "${PROJECT_ROOT}/src" -type f -name '*.cpp' \
            ! -name 'parser_generated.cpp' \
            ! -name 'lexer_scanner.cpp' \
            | sort
    )

    if [[ "${#source_files[@]}" -eq 0 ]]; then
        echo "no source files found for clang-tidy" >&2
        return 1
    fi

    for source_file in "${source_files[@]}"; do
        echo "[clang-tidy] ${source_file}"
        clang-tidy \
            --checks="${CLANG_TIDY_CHECKS}" \
            --header-filter="^${PROJECT_ROOT}/src/.*" \
            --exclude-header-filter="^${PROJECT_ROOT}/src/frontend/parser/parser\\.tab\\.h$" \
            "${EXTRA_CLANG_TIDY_ARGS[@]}" \
            -p "${BUILD_DIR}" \
            "${source_file}"
    done
}

run_cppcheck() {
    echo "==> cppcheck"
    cppcheck \
        --project="${COMPILE_COMMANDS}" \
        --enable=warning,performance,portability \
        --inconclusive \
        --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        -i"${PROJECT_ROOT}/src/frontend/parser/parser_generated.cpp" \
        -i"${PROJECT_ROOT}/src/frontend/lexer/lexer_scanner.cpp"
}

run_iwyu() {
    echo "==> include-what-you-use"
    python3 "${SCRIPT_DIR}/run_iwyu.py" "${COMPILE_COMMANDS}"
}

main() {
    require_tool cmake
    require_tool clang-tidy
    require_tool cppcheck
    require_tool include-what-you-use
    require_tool python3

    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
    cmake --build "${BUILD_DIR}"

    if [[ ! -f "${COMPILE_COMMANDS}" ]]; then
        echo "missing compile commands after build: ${COMPILE_COMMANDS}" >&2
        return 1
    fi

    run_clang_tidy
    run_cppcheck
    run_iwyu
}

main "$@"
