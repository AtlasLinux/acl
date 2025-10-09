# Compiler and flags
CC := gcc
CFLAGS := -fPIC -Wall -Wextra -O2
LDFLAGS := -shared

# Directories
SRC_DIR := src
BUILD_DIR := build

# Source and object files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Targets
TARGET_SO := $(BUILD_DIR)/libacl.so
TARGET_A  := $(BUILD_DIR)/libacl.a

# Default target builds both
all: $(TARGET_SO) $(TARGET_A)

# Shared library
$(TARGET_SO): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Static library
$(TARGET_A): $(OBJS)
	ar rcs $@ $^

# Compile .c to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
