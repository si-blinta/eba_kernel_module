# Makefile for building the user application

# Compiler
CC = gcc

# Directories
INCLUDE_DIR = ../include
LIB_DIR = $(shell pwd)/../lib

# Compiler flags: -O2 optimization, -Wall for warnings, include our header directory.
CFLAGS = -O2 -Wall -I$(INCLUDE_DIR)

# Linker flags:
# -L$(LIB_DIR) specifies the library search directory.
# -leba links against libeba.so.
# -Wl,-rpath,$(LIB_DIR) embeds the absolute runtime library search path into the executable.
LDFLAGS = -L$(LIB_DIR) -leba -Wl,-rpath,$(LIB_DIR)

# Target name and source file
TARGET = user
SRC = user.c

.PHONY: all clean

all: $(TARGET)
	@echo "Build complete.Run export LD_LIBRARY_PATH=/home/nader/eba_kernel_module/lib and Run ./$(TARGET) to execute the application."

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
	@echo "Clean complete."
