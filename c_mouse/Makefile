# Makefile for mouse_support C23 extension
#
# Build: make
# Install: make install
# Clean: make clean

CC = gcc
CFLAGS = -std=c23 -O2 -march=native -fPIC -Wall -Wextra -Wpedantic
CFLAGS += -I/home/mod/personal/PROGRAMMING/SYSTEM\ PROGRAMS/LINUX/μEmacs/include
LDFLAGS = -shared

TARGET = c_mouse.so
SRC = mouse.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@echo "Extension built: $(TARGET)"
	@echo "It will be auto-loaded from ~/.config/muemacs/extensions/"
