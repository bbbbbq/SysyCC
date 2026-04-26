CC ?= cc
AR ?= ar
CPPFLAGS ?= -Iinclude -DNORTHSTAR_SCALE=3
CFLAGS ?= -O0 -std=gnu99 -pipe -ffunction-sections -fdata-sections -fno-common
DEPFLAGS ?= -MMD -MP

BUILD_DIR := build
LIB_OBJS := \
	$(BUILD_DIR)/array.o \
	$(BUILD_DIR)/stats.o \
	$(BUILD_DIR)/sort.o \
	$(BUILD_DIR)/search.o \
	$(BUILD_DIR)/hash.o \
	$(BUILD_DIR)/graph.o \
	$(BUILD_DIR)/pipeline.o
APP_OBJ := $(BUILD_DIR)/main.o
APP := $(BUILD_DIR)/northstar_demo
LIB := $(BUILD_DIR)/libnorthstar.a

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c include/northstar.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: app/main.c include/northstar.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(APP): $(APP_OBJ) $(LIB)
	$(CC) $(APP_OBJ) $(LIB) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(LIB_OBJS:.o=.d) $(APP_OBJ:.o=.d)
