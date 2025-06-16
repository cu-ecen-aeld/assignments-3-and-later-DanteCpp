# Compiler and flags
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror

# Target and source
TARGET := writer
SRC := writer.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean

# Default target
all: $(TARGET)

# Build the target
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Clean target
clean:
	rm -f $(TARGET) $(OBJ)
