DEV_BUILD_DIR := build-ninja
TARGET := $(DEV_BUILD_DIR)/SysyCC
FORMAT_FILES := $(shell find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)
TEST_ARGS ?=

.PHONY: all ensure-ninja configure-ninja build build-ninja run run-ninja test-tier1 test-tier2 test-full test-aarch64-ll test-aarch64-single-source test-aarch64-single-source-smoke test-aarch64-single-source-full lua-smoke real-project-compile-times test clean clean-ninja format check

all: run

ensure-ninja:
	@command -v ninja >/dev/null 2>&1 || { echo "error: ninja is required for the top-level development build entry" >&2; exit 1; }

configure-ninja: ensure-ninja
	cmake -S . -B $(DEV_BUILD_DIR) -G Ninja

build-ninja: configure-ninja
	cmake --build $(DEV_BUILD_DIR)

build: build-ninja

run-ninja: build-ninja
	./$(TARGET)

run: run-ninja

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
