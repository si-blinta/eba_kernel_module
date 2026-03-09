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

# FFmpeg and other required system libraries for streaming targets
LDFLAGS_FFMPEG = -lavformat -lavcodec -lswscale -lavutil -ljpeg -lX11
LDFLAGS_FFMPEG_CLIENT = -lavformat -lavcodec -lswscale -lavutil -ljpeg -lSDL2

# eba_tcp.c is a shared library module, not a standalone executable.
# Exclude it from the list of programs produced by the generic pattern rule.
EBA_TCP_LIB_SRC = eba_tcp.c
EBA_TCP_LIB_OBJ = eba_tcp.o

# Collect all C sources that have a main() function.
C_SOURCES = $(filter-out $(EBA_TCP_LIB_SRC), $(wildcard *.c))
TARGETS   = $(basename $(C_SOURCES))

.PHONY: all clean

all: $(EBA_TCP_LIB_OBJ) $(TARGETS)
	@echo "Build complete. Run 'export LD_LIBRARY_PATH=$(LIB_DIR)' and then './<program>' to execute any application."

# Compile the shared TCP helper object.
$(EBA_TCP_LIB_OBJ): $(EBA_TCP_LIB_SRC) $(INCLUDE_DIR)/eba_tcp.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Pattern rule to compile each standalone C file into an executable.
%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Explicit rules for programs that depend on the EBA TCP library.
eba_httpd: eba_httpd.c $(EBA_TCP_LIB_OBJ) $(INCLUDE_DIR)/eba_tcp.h
	$(CC) $(CFLAGS) -o $@ eba_httpd.c $(EBA_TCP_LIB_OBJ) $(LDFLAGS)

eba_http_client: eba_http_client.c $(EBA_TCP_LIB_OBJ) $(INCLUDE_DIR)/eba_tcp.h
	$(CC) $(CFLAGS) -o $@ eba_http_client.c $(EBA_TCP_LIB_OBJ) $(LDFLAGS)

# Explicit rules for streaming targets with FFmpeg and extra libs
server_streaming: server_streaming.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDFLAGS_FFMPEG)

client_streaming: client_streaming.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDFLAGS_FFMPEG_CLIENT)

clean:
	rm -f $(TARGETS) $(EBA_TCP_LIB_OBJ)
	@echo "Clean complete."
