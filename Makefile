BUILD_DIR := build
TARGET := $(BUILD_DIR)/SysyCC
FORMAT_FILES := $(shell find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)

.PHONY: all build run clean format

all: run

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

run: build
	./$(TARGET)

format:
	clang-format -i $(FORMAT_FILES)

clean:
	rm -rf $(BUILD_DIR)
