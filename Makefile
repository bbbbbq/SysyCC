BUILD_DIR := build
TARGET := $(BUILD_DIR)/SysyCC
INPUT_FILE ?= tests/arithmetic.sy

.PHONY: all build run clean

all: run

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

run: build
	./$(TARGET) lex $(INPUT_FILE)

clean:
	rm -rf $(BUILD_DIR)
