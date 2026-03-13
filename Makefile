BUILD_DIR := build
TARGET := $(BUILD_DIR)/SysyCC

.PHONY: all build run clean

all: run

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

run: build
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
