# Makefile for building the user application

# Compiler
CC = gcc

# Directories
INCLUDE_DIR = ../include
LIB_DIR = $(shell pwd)/../lib

# Compiler flags: -O2 optimization, -Wall for warnings, include our header directory.
CFLAGS = -O2 -I$(INCLUDE_DIR) -Wall

# Linker flags:
# -L$(LIB_DIR) specifies the library search directory.
# -leba links against libeba.so.
# -Wl,-rpath,$(LIB_DIR) embeds the absolute runtime library search path into the executable.
LDFLAGS = -L$(LIB_DIR) -leba -Wl,-rpath,$(LIB_DIR)

# Collect all C source files and derive executables names from them.
C_SOURCES = $(wildcard *.c)
TARGETS   = $(basename $(C_SOURCES))

.PHONY: all clean

all: $(TARGETS)
	@echo "Build complete. Run 'export LD_LIBRARY_PATH=$(LIB_DIR)' and then './<program>' to execute any application."

# Pattern rule to compile each C file into an executable.
%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	@echo "Clean complete."
