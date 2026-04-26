CC ?= cc
CASE_NAME ?= driver_gap_case_wave3
BUILD_DIR ?= build
FIXTURE_DIR ?= ../../fixtures
CFLAGS ?= -std=gnu99 -O0 -Wall -Wextra -I$(FIXTURE_DIR)/include

APP := $(BUILD_DIR)/$(CASE_NAME)
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/lib.o

all: $(APP)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/main.o: $(FIXTURE_DIR)/src/main.c $(FIXTURE_DIR)/include/gap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib.o: $(FIXTURE_DIR)/src/lib.c $(FIXTURE_DIR)/include/gap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(APP): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

run: $(APP)
	./$(APP)

clean:
	rm -rf $(BUILD_DIR)
