# NEUROXUS AI Agent Extension - Crystal + C Bridge
# Version 2.0.0

CC = gcc
CRYSTAL = crystal
CFLAGS = -O2 -fPIC -Wall
CURDIR := $(shell pwd)

TARGET = ai_crystal.so
CRYSTAL_SRCS = ai_completion.cr agent.cr

all: $(TARGET)

# Compile bridge.c to object, then build Crystal linking against it
# Crystal will follow require "./agent" to include agent.cr
$(TARGET): bridge.c $(CRYSTAL_SRCS)
	$(CC) $(CFLAGS) -c -o bridge.o bridge.c
	$(CRYSTAL) build --release --no-debug \
		--link-flags="-shared" \
		--link-flags="$(CURDIR)/bridge.o" \
		-o $@ ai_completion.cr

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
