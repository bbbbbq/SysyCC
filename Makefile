DEV_BUILD_DIR := build-ninja
TARGET := $(DEV_BUILD_DIR)/SysyCC
FORMAT_FILES := $(shell find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)
TEST_ARGS ?=
SYSYCC_USE_COMPILER_CACHE ?= ON
SYSYCC_DEV_BUILD ?= ON
SYSYCC_COMPILER_CACHE ?= $(shell command -v ccache 2>/dev/null || command -v sccache 2>/dev/null)
CMAKE_CONFIGURE_ARGS := -DSYSYCC_USE_COMPILER_CACHE=$(SYSYCC_USE_COMPILER_CACHE) -DSYSYCC_DEV_BUILD=$(SYSYCC_DEV_BUILD)
ifeq ($(SYSYCC_USE_COMPILER_CACHE),ON)
ifneq ($(SYSYCC_COMPILER_CACHE),)
CMAKE_CONFIGURE_ARGS += -DCMAKE_C_COMPILER_LAUNCHER=$(SYSYCC_COMPILER_CACHE)
CMAKE_CONFIGURE_ARGS += -DCMAKE_CXX_COMPILER_LAUNCHER=$(SYSYCC_COMPILER_CACHE)
endif
endif

.PHONY: all ensure-ninja configure-ninja build build-ninja run run-ninja profile-self-build test-tier1 test-tier2 test-full test-aarch64-ll test-aarch64-single-source test-aarch64-single-source-smoke test-aarch64-single-source-full lua-smoke lua-incremental pass-report-diff real-project-compile-times test clean clean-ninja format check

all: run

ensure-ninja:
	@command -v ninja >/dev/null 2>&1 || { echo "error: ninja is required for the top-level development build entry" >&2; exit 1; }

configure-ninja: ensure-ninja
	cmake -S . -B $(DEV_BUILD_DIR) -G Ninja $(CMAKE_CONFIGURE_ARGS)

build-ninja: configure-ninja
	cmake --build $(DEV_BUILD_DIR)

build: build-ninja

run-ninja: build-ninja
	./$(TARGET)

run: run-ninja

profile-self-build:
	./scripts/profile_self_build.sh

test-tier1:
	./tests/run_tier1.sh $(TEST_ARGS)

test-tier2:
	./tests/run_tier2.sh $(TEST_ARGS)

test-full:
	./tests/run_full.sh $(TEST_ARGS)

test-aarch64-ll:
	./tests/run_all.sh --stage aarch64_backend_ll $(TEST_ARGS)

test-aarch64-single-source:
	./tests/run_all.sh --stage aarch64_backend_single_source $(TEST_ARGS)

test-aarch64-single-source-smoke:
	./tests/aarch64_backend_single_source/smoke/run.sh $(TEST_ARGS)

test-aarch64-single-source-full:
	./tests/aarch64_backend_single_source/imported_suite/run.sh $(TEST_ARGS)

lua-smoke:
	./tests/manual/external_real_project_probe/lua_smoke.sh $(TEST_ARGS)

lua-incremental:
	./tests/manual/external_real_project_probe/incremental_lua.sh $(TEST_ARGS)

pass-report-diff:
	./tests/manual/external_real_project_probe/diff_pass_reports.py $(TEST_ARGS)

real-project-compile-times:
	./tests/manual/external_real_project_probe/profile_compile_times.sh $(TEST_ARGS)

test: test-tier1

format:
	clang-format -i $(FORMAT_FILES)

check:
	./scripts/run_static_checks.sh

clean-ninja:
	rm -rf $(DEV_BUILD_DIR)

clean:
	rm -rf build $(DEV_BUILD_DIR)
