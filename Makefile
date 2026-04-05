DEV_BUILD_DIR := build-ninja
TARGET := $(DEV_BUILD_DIR)/SysyCC
FORMAT_FILES := $(shell find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)

.PHONY: all ensure-ninja configure-ninja build build-ninja run run-ninja clean clean-ninja format check

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

format:
	clang-format -i $(FORMAT_FILES)

check:
	./scripts/run_static_checks.sh

clean-ninja:
	rm -rf $(DEV_BUILD_DIR)

clean:
	rm -rf build $(DEV_BUILD_DIR)
