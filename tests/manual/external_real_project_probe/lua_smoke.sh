#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

DOCKER_CONTAINER="${SYSYCC_REAL_PROJECT_DOCKER_CONTAINER:-qemu_dev}"
DOCKER_PROJECT_ROOT="${SYSYCC_REAL_PROJECT_DOCKER_PROJECT_ROOT:-/code/SysyCC}"
WORK_DIR="${SYSYCC_REAL_PROJECT_WORK_DIR:-${PROJECT_ROOT}/build/external-real-project-probe}"
LUA_BINARY="${SYSYCC_LUA_BINARY:-}"
SMOKE_TIMEOUT="${SYSYCC_LUA_SMOKE_TIMEOUT:-20}"

run_in_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "error: docker is required for lua-smoke outside the probe container" >&2
        exit 1
    fi
    if ! docker inspect "${DOCKER_CONTAINER}" >/dev/null 2>&1; then
        echo "error: docker container '${DOCKER_CONTAINER}' is not available" >&2
        echo "hint: run tests/manual/external_real_project_probe/verify.sh first" >&2
        exit 1
    fi
    docker exec \
        -e SYSYCC_REAL_PROJECT_IN_DOCKER=1 \
        -e SYSYCC_LUA_BINARY="${LUA_BINARY}" \
        -e SYSYCC_LUA_SMOKE_TIMEOUT="${SMOKE_TIMEOUT}" \
        -w "${DOCKER_PROJECT_ROOT}" \
        "${DOCKER_CONTAINER}" \
        bash "tests/manual/external_real_project_probe/lua_smoke.sh"
}

find_lua_binary() {
    if [[ -n "${LUA_BINARY}" ]]; then
        printf '%s\n' "${LUA_BINARY}"
        return
    fi

    local candidates=(
        "${WORK_DIR}/repos/lua/lua"
        "${PROJECT_ROOT}/build/external-real-project-probe/repos/lua/lua"
        "/tmp/lua-sysycc-current"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return
        fi
    done
}

run_lua_smoke() {
    local lua_bin
    lua_bin="$(find_lua_binary)"
    if [[ -z "${lua_bin}" || ! -x "${lua_bin}" ]]; then
        echo "error: no SysyCC-built Lua binary found" >&2
        echo "hint: run tests/manual/external_real_project_probe/verify.sh first, or set SYSYCC_LUA_BINARY=/path/to/lua" >&2
        exit 1
    fi

    local smoke_file
    smoke_file="$(mktemp "${TMPDIR:-/tmp}/sysycc-lua-smoke.XXXXXX.lua")"
    cat >"${smoke_file}" <<'EOF'
local function check(name, got, expect)
  if got ~= expect then
    io.stderr:write(name .. " expected " .. tostring(expect) .. " got " .. tostring(got) .. "\n")
    os.exit(1)
  end
end

local t = {1, 2, 3}
t[#t + 1] = 4
check("table length", #t, 4)
check("table sum", t[1] + t[4], 5)

local function make_counter(seed)
  local x = seed
  return function(step)
    x = x + step
    return x
  end
end
local c = make_counter(10)
check("closure one", c(3), 13)
check("closure two", c(4), 17)

local co = coroutine.create(function(a, b)
  coroutine.yield(a + b)
  return a * b
end)
local ok, value = coroutine.resume(co, 6, 7)
check("coroutine yield ok", ok, true)
check("coroutine yield", value, 13)
ok, value = coroutine.resume(co)
check("coroutine return ok", ok, true)
check("coroutine return", value, 42)

local packed = string.pack("<i4I4i8I8fd", -1, 2, -3, 4, 1.5, 2.5)
check("pack size", #packed, 36)
local a, b, c8, d8, f, d = string.unpack("<i4I4i8I8fd", packed)
check("unpack i4", a, -1)
check("unpack I4", b, 2)
check("unpack i8", c8, -3)
check("unpack I8", d8, 4)
check("unpack f", math.floor(f * 10), 15)
check("unpack d", math.floor(d * 10), 25)

local dumped = string.dump(function(x) return x * 2 + 1 end)
check("dump sig1", string.byte(dumped, 1), 27)
check("dump sig2", string.byte(dumped, 2), 76)
local loaded = assert(load(dumped))
check("load dumped", loaded(20), 41)

local x = -0.0
check("negative zero tostring", tostring(x), "-0.0")
check("negative zero inverse", tostring(1 / x), "-inf")

local ftmp = assert(io.tmpfile())
ftmp:write("Lua", "-", "SysyCC")
ftmp:seek("set")
check("tmpfile", ftmp:read("a"), "Lua-SysyCC")

print("lua behavior smoke ok")
EOF

    timeout "${SMOKE_TIMEOUT}" "${lua_bin}" "${smoke_file}"
    rm -f "${smoke_file}"
}

if [[ "${SYSYCC_REAL_PROJECT_IN_DOCKER:-0}" != "1" ]]; then
    run_in_docker
else
    run_lua_smoke
fi
