#!/usr/bin/env bash

set -euo pipefail

RISCV64_SINGLE_SOURCE_EXTRA_INCLUDE_DIRS=()
RISCV64_SINGLE_SOURCE_EXTRA_CFLAGS=()
RISCV64_SINGLE_SOURCE_EXTRA_COMPANION_SOURCES=()

riscv_single_source_stage_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

riscv_single_source_source_stage_root() {
    local explicit_root="${SYSYCC_RISCV64_SINGLE_SOURCE_SOURCE_STAGE_ROOT:-}"
    if [[ -n "${explicit_root}" ]]; then
        printf '%s\n' "${explicit_root}"
        return 0
    fi
    cd "$(dirname "${BASH_SOURCE[0]}")/../aarch64_backend_single_source" && pwd
}

source "$(riscv_single_source_source_stage_root)/common.sh"

find_riscv64_host_clang() {
    if [[ -n "${SYSYCC_HOST_CLANG:-}" ]] &&
        command -v "${SYSYCC_HOST_CLANG}" >/dev/null 2>&1; then
        command -v "${SYSYCC_HOST_CLANG}"
        return 0
    fi
    if [[ -x /opt/homebrew/opt/llvm/bin/clang ]]; then
        printf '%s\n' /opt/homebrew/opt/llvm/bin/clang
        return 0
    fi
    command -v clang >/dev/null 2>&1 || return 1
    command -v clang
}

riscv_single_source_batch_size() {
    printf '%s\n' "${SYSYCC_RISCV64_SINGLE_SOURCE_BATCH_SIZE:-100}"
}

riscv_single_source_batch_index() {
    printf '%s\n' "${SYSYCC_RISCV64_SINGLE_SOURCE_BATCH_INDEX:-1}"
}

riscv_single_source_start_index() {
    printf '%s\n' "${SYSYCC_RISCV64_SINGLE_SOURCE_START_INDEX:-1}"
}

riscv_single_source_run_timeout_seconds() {
    if [[ -n "${SYSYCC_RISCV64_SINGLE_SOURCE_RUN_TIMEOUT_SECONDS:-}" ]]; then
        printf '%s\n' "${SYSYCC_RISCV64_SINGLE_SOURCE_RUN_TIMEOUT_SECONDS}"
        return 0
    fi
    printf '20\n'
}

find_riscv64_user_qemu() {
    if [[ -n "${SYSYCC_QEMU_RISCV64:-}" ]] &&
        command -v "${SYSYCC_QEMU_RISCV64}" >/dev/null 2>&1; then
        command -v "${SYSYCC_QEMU_RISCV64}"
        return 0
    fi
    command -v qemu-riscv64 >/dev/null 2>&1 || return 1
    command -v qemu-riscv64
}

find_riscv64_linux_sysroot() {
    local candidate=""

    if [[ -n "${SYSYCC_RISCV64_SYSROOT:-}" ]] &&
        [[ -d "${SYSYCC_RISCV64_SYSROOT}" ]]; then
        printf '%s\n' "${SYSYCC_RISCV64_SYSROOT}"
        return 0
    fi

    for candidate in \
        /usr/riscv64-linux-gnu \
        /usr/local/riscv64-linux-gnu \
        /opt/riscv/sysroot \
        /opt/homebrew/riscv64-linux-gnu \
        /opt/homebrew/opt/riscv64-linux-gnu-toolchain/riscv64-linux-gnu/sysroot; do
        if [[ -d "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

have_riscv64_docker_runtime() {
    command -v docker >/dev/null 2>&1
}

riscv_single_source_docker_image() {
    printf '%s\n' "${SYSYCC_RISCV64_DOCKER_IMAGE:-sysycc-riscv64-user-runner:ubuntu24.04}"
}

ensure_riscv64_docker_image() {
    local stage_root="$1"
    local image=""
    image="$(riscv_single_source_docker_image)"

    docker image inspect "${image}" >/dev/null 2>&1 && return 0

    docker build \
        -t "${image}" \
        -f "${stage_root}/Dockerfile.user" \
        "${stage_root}"
}

riscv_single_source_container_path() {
    local project_root="$1"
    local host_path="$2"

    case "${host_path}" in
        "${project_root}"/*)
            printf '/workspace%s\n' "${host_path#"${project_root}"}"
            ;;
        *)
            echo "error: path is outside the mounted project root: ${host_path}" >&2
            return 1
            ;;
    esac
}

run_riscv64_docker_command() {
    local project_root="$1"
    shift

    local image=""
    image="$(riscv_single_source_docker_image)"
    docker run --rm \
        --init \
        --user "$(id -u):$(id -g)" \
        -v "${project_root}:/workspace" \
        -w /workspace \
        "${image}" \
        "$@"
}

find_riscv64_objcopy() {
    if command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
        command -v riscv64-linux-gnu-objcopy
        return 0
    fi
    if command -v riscv64-elf-objcopy >/dev/null 2>&1; then
        command -v riscv64-elf-objcopy
        return 0
    fi
    if command -v llvm-objcopy >/dev/null 2>&1; then
        command -v llvm-objcopy
        return 0
    fi
    if command -v objcopy >/dev/null 2>&1; then
        command -v objcopy
        return 0
    fi
    return 1
}

normalize_riscv64_ctor_sections() {
    local runtime_mode="$1"
    local project_root="$2"
    local object_file="$3"

    if ! /opt/homebrew/bin/riscv64-elf-readelf -S "${object_file}" 2>/dev/null | grep -Eq '[[:space:]]\.ctors([[:space:]]|$)'; then
        return 0
    fi

    if [[ "${runtime_mode}" == "docker" ]]; then
        local object_container=""
        object_container="$(riscv_single_source_container_path "${project_root}" "${object_file}")"
        run_riscv64_docker_command "${project_root}" \
            riscv64-linux-gnu-objcopy \
            --rename-section .ctors=.init_array,alloc,load,data,contents \
            --rename-section .dtors=.fini_array,alloc,load,data,contents \
            --rename-section .rela.ctors=.rela.init_array \
            --rename-section .rela.dtors=.rela.fini_array \
            "${object_container}"
        return 0
    fi

    local objcopy_tool=""
    objcopy_tool="$(find_riscv64_objcopy)"
    "${objcopy_tool}" \
        --rename-section .ctors=.init_array,alloc,load,data,contents \
        --rename-section .dtors=.fini_array,alloc,load,data,contents \
        --rename-section .rela.ctors=.rela.init_array \
        --rename-section .rela.dtors=.rela.fini_array \
        "${object_file}"
}

run_host_clang_riscv64() {
    local host_clang="$1"
    local sysroot="$2"
    shift 2

    local -a command=(
        "${host_clang}"
        --target=riscv64-unknown-linux-gnu
        --sysroot="${sysroot}"
    )
    if [[ -n "${SYSYCC_RISCV64_GCC_TOOLCHAIN:-}" ]]; then
        command+=("--gcc-toolchain=${SYSYCC_RISCV64_GCC_TOOLCHAIN}")
    fi
    "${command[@]}" "$@"
}

emit_riscv_single_source_batch_entries() {
    local manifest_file="$1"
    local start_index="$2"
    local batch_index="$3"
    local batch_size="$4"
    local start_line=$(( start_index + (batch_index - 1) * batch_size ))
    local end_line=$(( start_line + batch_size - 1 ))

    awk -F'|' -v start="${start_line}" -v end="${end_line}" '
        NF > 0 && $1 != "" && $1 !~ /^#/ {
            count++
            if (count >= start && count <= end) {
                print $0
            }
        }
    ' "${manifest_file}"
}

emit_riscv_single_source_discovered_entries() {
    local source_stage_root="$1"

    python3 - "${source_stage_root}" <<'PY'
import os
import sys
from pathlib import Path

stage_root = Path(sys.argv[1])
upstream_root = stage_root / "upstream" / "SingleSource"
std_overrides = {
    "SingleSource/UnitTests/2002-12-13-MishaTest.c": "gnu89",
    "SingleSource/UnitTests/2007-01-04-KNR-Args.c": "gnu89",
    "SingleSource/UnitTests/2008-04-18-LoopBug.c": "gnu89",
    "SingleSource/UnitTests/2008-04-20-LoopBug2.c": "gnu89",
    "SingleSource/UnitTests/AtomicOps.c": "gnu89",
    "SingleSource/Regression/C/DuffsDevice.c": "gnu89",
    "SingleSource/Regression/C/PR10189.c": "gnu89",
}

def bucket(rel: str) -> tuple[int, str]:
    if rel.startswith("SingleSource/UnitTests/"):
        return (0, rel)
    if rel.startswith("SingleSource/Regression/"):
        return (1, rel)
    if rel.startswith("SingleSource/Benchmarks/"):
        return (2, rel)
    return (3, rel)

entries = []
for path in sorted(upstream_root.rglob("*.c")):
    rel = path.relative_to(stage_root / "upstream").as_posix()
    if rel.startswith("SingleSource/Support/"):
        continue
    if rel.startswith("SingleSource/Benchmarks/Polybench/utilities/"):
        continue
    if rel.endswith("-lib.c"):
        continue
    if "/AArch64/" in rel or "/ARM/" in rel:
        continue
    if "aarch64-" in rel.lower():
        continue
    sibling_companion = path.with_name(path.stem + "-lib.c")
    if sibling_companion.exists():
        continue
    entries.append(rel)

for rel in sorted(entries, key=bucket):
    std = std_overrides.get(rel, 'gnu11')
    if rel.startswith("SingleSource/Regression/C/gcc-c-torture/execute/"):
        std = "gnu89"
    print(f"{std}|{rel}|-")
PY
}

run_riscv_single_source_runtime_case() {
    local input_binary="$1"
    local sysroot="$2"
    local stdout_file="$3"
    local stderr_file="$4"
    local status_file="$5"
    shift 5

    local qemu=""
    qemu="$(find_riscv64_user_qemu 2>/dev/null || true)"
    if [[ -z "${qemu}" ]]; then
        echo "missing qemu-riscv64 runtime" >"${stderr_file}"
        printf '125\n' >"${status_file}"
        return 125
    fi

    local timeout_seconds=""
    timeout_seconds="$(riscv_single_source_run_timeout_seconds)"

    python3 - "${qemu}" "${input_binary}" "${sysroot}" "${stdout_file}" \
        "${stderr_file}" "${status_file}" "${timeout_seconds}" "$@" <<'PY'
import os
import pathlib
import signal
import subprocess
import sys

qemu, input_binary, sysroot, stdout_path, stderr_path, status_path, timeout_text, *runtime_args = sys.argv[1:]
timeout_seconds = float(timeout_text)
command = [qemu, "-L", sysroot, input_binary, *runtime_args]

status_code = 0
with open(stdout_path, "wb") as stdout_file, open(stderr_path, "wb") as stderr_file:
    process = None
    try:
        process = subprocess.Popen(
            command,
            stdout=stdout_file,
            stderr=stderr_file,
            start_new_session=True,
        )
        status_code = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        stderr_file.write(
            f"timed out after {timeout_seconds:g}s\n".encode()
        )
        status_code = 124

pathlib.Path(status_path).write_text(f"{status_code}\n")
PY
}

run_riscv_single_source_runtime_case_via_docker() {
    local project_root="$1"
    local input_binary="$2"
    local stdout_file="$3"
    local stderr_file="$4"
    local status_file="$5"
    shift 5

    local timeout_seconds=""
    timeout_seconds="$(riscv_single_source_run_timeout_seconds)"

    local input_binary_container=""
    input_binary_container="$(riscv_single_source_container_path \
        "${project_root}" "${input_binary}")"

    python3 - "${project_root}" "${input_binary_container}" "${stdout_file}" \
        "${stderr_file}" "${status_file}" "${timeout_seconds}" "$@" <<'PY'
import os
import pathlib
import signal
import subprocess
import uuid
import sys

project_root, input_binary, stdout_path, stderr_path, status_path, timeout_text, *runtime_args = sys.argv[1:]
timeout_seconds = float(timeout_text)
image = os.environ.get("SYSYCC_RISCV64_DOCKER_IMAGE", "sysycc-riscv64-user-runner:ubuntu24.04")
container_name = f"sysycc-riscv64-run-{uuid.uuid4().hex[:12]}"
command = [
    "docker", "run", "--rm",
    "--init",
    "--name", container_name,
    "--user", f"{os.getuid()}:{os.getgid()}",
    "-v", f"{project_root}:/workspace",
    "-w", "/workspace",
    image,
    "qemu-riscv64", "-L", "/usr/riscv64-linux-gnu", input_binary, *runtime_args,
]

status_code = 0
with open(stdout_path, "wb") as stdout_file, open(stderr_path, "wb") as stderr_file:
    process = None
    try:
        process = subprocess.Popen(
            command,
            stdout=stdout_file,
            stderr=stderr_file,
            start_new_session=True,
        )
        status_code = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        subprocess.run(
            ["docker", "rm", "-f", container_name],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        stderr_file.write(
            f"timed out after {timeout_seconds:g}s\n".encode()
        )
        status_code = 124

pathlib.Path(status_path).write_text(f"{status_code}\n")
PY
}

run_riscv_single_source_snapshot_case() {
    local stage_root="$1"
    local source_stage_root="$2"
    local project_root="$3"
    local build_dir="$4"
    local runtime_mode="$5"
    local sysroot="$6"
    local host_clang="$7"
    local source_rel="$8"
    local c_std="$9"
    local argv_text="${10:-}"

    local case_id=""
    local prepared_source_file=""
    local case_build_dir=""
    local log_file=""
    local ll_file=""
    local sysycc_obj_file=""
    local clang_bin=""
    local sysycc_bin=""
    local sysycc_stdout=""
    local sysycc_stderr=""
    local sysycc_status=""
    local clang_stdout=""
    local clang_stderr=""
    local clang_status=""
    local -a runtime_args=()
    local -a extra_compile_args=()
    local -a compatibility_compile_args=(
        "-Wno-return-type"
        "-Dalloca=__builtin_alloca"
        "-Wno-int-conversion"
    )
    local -a companion_sources=()
    local -a companion_objects=()
    local -a extra_compile_args_container=()
    local prepared_source_container=""
    local ll_container=""
    local sysycc_obj_container=""
    local clang_bin_container=""
    local sysycc_bin_container=""
    local companion_source=""
    local companion_case_id=""
    local companion_object=""
    local companion_source_container=""
    local companion_object_container=""
    local include_dir=""

    case_id="$(single_source_case_id "${source_rel}")"
    case_build_dir="${stage_root}/build/${case_id}"
    log_file="${build_dir}/test_logs/riscv64_backend_single_source_${case_id}.log"
    ll_file="${case_build_dir}/${case_id}.ll"
    sysycc_obj_file="${case_build_dir}/${case_id}.sysycc.o"
    clang_bin="${case_build_dir}/${case_id}.clang.bin"
    sysycc_bin="${case_build_dir}/${case_id}.sysycc.bin"
    sysycc_stdout="${case_build_dir}/${case_id}.sysycc.stdout"
    sysycc_stderr="${case_build_dir}/${case_id}.sysycc.stderr"
    sysycc_status="${case_build_dir}/${case_id}.sysycc.status"
    clang_stdout="${case_build_dir}/${case_id}.clang.stdout"
    clang_stderr="${case_build_dir}/${case_id}.clang.stderr"
    clang_status="${case_build_dir}/${case_id}.clang.status"

    mkdir -p "${case_build_dir}" "$(dirname "${log_file}")"
    prepared_source_file="$(prepare_single_source_case_source \
        "${source_stage_root}" "${source_rel}" "${case_build_dir}")"
    case "${source_rel}" in
        "SingleSource/Regression/C/gcc-c-torture/execute/ieee/fp-cmp-7.c")
            local patched_source="${case_build_dir}/$(basename "${source_rel}" .c).riscv64.c"
            cp "${prepared_source_file}" "${patched_source}"
            cat >>"${patched_source}" <<'EOF'

void link_error(void) { __builtin_abort(); }
EOF
            prepared_source_file="${patched_source}"
            ;;
    esac
    if [[ -n "${argv_text}" && "${argv_text}" != "-" ]]; then
        read -r -a runtime_args <<<"${argv_text}"
    fi

    prepared_source_container="$(riscv_single_source_container_path \
        "${project_root}" "${prepared_source_file}")"
    ll_container="$(riscv_single_source_container_path \
        "${project_root}" "${ll_file}")"
    sysycc_obj_container="$(riscv_single_source_container_path \
        "${project_root}" "${sysycc_obj_file}")"
    clang_bin_container="$(riscv_single_source_container_path \
        "${project_root}" "${clang_bin}")"
    sysycc_bin_container="$(riscv_single_source_container_path \
        "${project_root}" "${sysycc_bin}")"

    populate_single_source_case_support "${source_stage_root}" "${source_rel}"
    for include_dir in "${SINGLE_SOURCE_EXTRA_INCLUDE_DIRS[@]-}"; do
        [[ -z "${include_dir}" ]] && continue
        extra_compile_args+=("-I${include_dir}")
        extra_compile_args_container+=(
            "-I$(riscv_single_source_container_path "${project_root}" "${include_dir}")"
        )
    done
    for include_dir in "${SINGLE_SOURCE_EXTRA_CFLAGS[@]-}"; do
        [[ -z "${include_dir}" ]] && continue
        extra_compile_args+=("${include_dir}")
        extra_compile_args_container+=("${include_dir}")
    done
    for companion_source in "${SINGLE_SOURCE_EXTRA_COMPANION_SOURCES[@]-}"; do
        [[ -z "${companion_source}" ]] && continue
        companion_sources+=("${source_stage_root}/upstream/${companion_source}")
    done

    {
        echo "==> Running ${source_rel}"
        if [[ "${runtime_mode}" == "docker" ]]; then
            run_riscv64_docker_command "${project_root}" \
                clang \
                --target=riscv64-linux-gnu \
                --gcc-toolchain=/usr \
                -std="${c_std}" \
                "${compatibility_compile_args[@]}" \
                -S -emit-llvm -O0 \
                -Xclang -disable-O0-optnone \
                -fno-stack-protector \
                -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -fno-builtin \
                "${extra_compile_args_container[@]-}" \
                "${prepared_source_container}" \
                -o "${ll_container}" || return 1
        else
            run_host_clang_riscv64 "${host_clang}" "${sysroot}" \
                -std="${c_std}" \
                "${compatibility_compile_args[@]}" \
                -S -emit-llvm -O0 \
                -Xclang -disable-O0-optnone \
                -fno-stack-protector \
                -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -fno-builtin \
                "${extra_compile_args[@]}" \
                "${prepared_source_file}" \
                -o "${ll_file}" || return 1
        fi
        assert_file_nonempty "${ll_file}" || return 1

        "${build_dir}/sysycc-riscv64c" \
            -c \
            --target riscv64-unknown-linux-gnu \
            -o "${sysycc_obj_file}" \
            "${ll_file}" || return 1
        assert_file_nonempty "${sysycc_obj_file}" || return 1
        normalize_riscv64_ctor_sections "${runtime_mode}" "${project_root}" \
            "${sysycc_obj_file}" || return 1

        for companion_source in "${companion_sources[@]-}"; do
            [[ -z "${companion_source}" ]] && continue
            companion_case_id="$(single_source_case_id \
                "${companion_source#${source_stage_root}/upstream/}")"
            companion_object="${case_build_dir}/${companion_case_id}.companion.o"
            companion_source_container="$(riscv_single_source_container_path \
                "${project_root}" "${companion_source}")"
            companion_object_container="$(riscv_single_source_container_path \
                "${project_root}" "${companion_object}")"
            if [[ "${runtime_mode}" == "docker" ]]; then
                run_riscv64_docker_command "${project_root}" \
                    clang \
                    --target=riscv64-linux-gnu \
                    --gcc-toolchain=/usr \
                    -std="${c_std}" \
                    "${compatibility_compile_args[@]}" \
                    -c \
                    -fno-stack-protector \
                    -fno-unwind-tables \
                    -fno-asynchronous-unwind-tables \
                    -fno-builtin \
                    "${extra_compile_args_container[@]-}" \
                    "${companion_source_container}" \
                    -o "${companion_object_container}" || return 1
            else
                run_host_clang_riscv64 "${host_clang}" "${sysroot}" \
                    -std="${c_std}" \
                    "${compatibility_compile_args[@]}" \
                    -fno-stack-protector \
                    -fno-unwind-tables \
                    -fno-asynchronous-unwind-tables \
                    -fno-builtin \
                    "${extra_compile_args[@]}" \
                    -c "${companion_source}" \
                    -o "${companion_object}" || return 1
            fi
            assert_file_nonempty "${companion_object}" || return 1
            companion_objects+=("${companion_object}")
        done

        if [[ "${runtime_mode}" == "docker" ]]; then
            local -a companion_sources_container=()
            for companion_source in "${companion_sources[@]-}"; do
                [[ -z "${companion_source}" ]] && continue
                companion_sources_container+=(
                    "$(riscv_single_source_container_path "${project_root}" "${companion_source}")"
                )
            done
            run_riscv64_docker_command "${project_root}" \
                clang \
                --target=riscv64-linux-gnu \
                --gcc-toolchain=/usr \
                -std="${c_std}" \
                "${compatibility_compile_args[@]}" \
                -fuse-ld=lld \
                -fno-pie \
                -no-pie \
                -fno-stack-protector \
                -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -fno-builtin \
                "${extra_compile_args_container[@]-}" \
                "${prepared_source_container}" \
                "${companion_sources_container[@]-}" \
                -lm \
                -o "${clang_bin_container}" || return 1
        else
            run_host_clang_riscv64 "${host_clang}" "${sysroot}" \
                -std="${c_std}" \
                "${compatibility_compile_args[@]}" \
                -fno-pie \
                -no-pie \
                -fno-stack-protector \
                -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -fno-builtin \
                "${extra_compile_args[@]}" \
                "${prepared_source_file}" \
                "${companion_sources[@]}" \
                -lm \
                -o "${clang_bin}" || return 1
        fi
        assert_file_nonempty "${clang_bin}" || return 1

        if [[ "${runtime_mode}" == "docker" ]]; then
            local -a companion_objects_container=()
            for companion_object in "${companion_objects[@]-}"; do
                [[ -z "${companion_object}" ]] && continue
                companion_objects_container+=(
                    "$(riscv_single_source_container_path "${project_root}" "${companion_object}")"
                )
            done
            run_riscv64_docker_command "${project_root}" \
                clang \
                --target=riscv64-linux-gnu \
                --gcc-toolchain=/usr \
                -fuse-ld=lld \
                -fno-pie \
                -no-pie \
                "${sysycc_obj_container}" \
                "${companion_objects_container[@]-}" \
                -lm \
                -o "${sysycc_bin_container}" || return 1
        else
            run_host_clang_riscv64 "${host_clang}" "${sysroot}" \
                -fno-pie \
                -no-pie \
                "${sysycc_obj_file}" \
                "${companion_objects[@]}" \
                -lm \
                -o "${sysycc_bin}" || return 1
        fi
        assert_file_nonempty "${sysycc_bin}" || return 1

        if [[ "${runtime_mode}" == "docker" ]]; then
            if [[ "${#runtime_args[@]}" -eq 0 ]]; then
                run_riscv_single_source_runtime_case_via_docker \
                    "${project_root}" "${clang_bin}" "${clang_stdout}" \
                    "${clang_stderr}" "${clang_status}" || return 1
                run_riscv_single_source_runtime_case_via_docker \
                    "${project_root}" "${sysycc_bin}" "${sysycc_stdout}" \
                    "${sysycc_stderr}" "${sysycc_status}" || return 1
            else
                run_riscv_single_source_runtime_case_via_docker \
                    "${project_root}" "${clang_bin}" "${clang_stdout}" \
                    "${clang_stderr}" "${clang_status}" \
                    "${runtime_args[@]}" || return 1
                run_riscv_single_source_runtime_case_via_docker \
                    "${project_root}" "${sysycc_bin}" "${sysycc_stdout}" \
                    "${sysycc_stderr}" "${sysycc_status}" \
                    "${runtime_args[@]}" || return 1
            fi
        elif [[ "${#runtime_args[@]}" -eq 0 ]]; then
            run_riscv_single_source_runtime_case "${clang_bin}" "${sysroot}" \
                "${clang_stdout}" "${clang_stderr}" "${clang_status}" || return 1
            run_riscv_single_source_runtime_case "${sysycc_bin}" "${sysroot}" \
                "${sysycc_stdout}" "${sysycc_stderr}" "${sysycc_status}" || return 1
        else
            run_riscv_single_source_runtime_case "${clang_bin}" "${sysroot}" \
                "${clang_stdout}" "${clang_stderr}" "${clang_status}" \
                "${runtime_args[@]}" || return 1
            run_riscv_single_source_runtime_case "${sysycc_bin}" "${sysroot}" \
                "${sysycc_stdout}" "${sysycc_stderr}" "${sysycc_status}" \
                "${runtime_args[@]}" || return 1
        fi

        compare_single_source_stdout_files "${clang_stdout}" "${sysycc_stdout}" || return 1
        diff -u "${clang_stderr}" "${sysycc_stderr}" || return 1
        diff -u "${clang_status}" "${sysycc_status}" || return 1
    } >"${log_file}" 2>&1
}
