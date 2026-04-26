CC ?= cc
CASE_NAME ?= gap_case
BUILD_DIR ?= build
CFLAGS ?= -std=gnu99 -O0 -Wall -Wextra -Iinclude
SOURCES ?= src/main.c src/lib.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
APP := $(BUILD_DIR)/$(CASE_NAME)

all: $(APP)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c include/gap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(APP): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

run: $(APP)
	./$(APP)

clean:
	rm -rf $(BUILD_DIR)

