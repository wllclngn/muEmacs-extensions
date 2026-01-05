# Git Integration Extension (C)
# Builds a shared library for uEmacs

CC = gcc
CFLAGS = -std=c23 -O2 -march=native -fPIC -Wall -Wextra -Wpedantic
CFLAGS += -I/home/mod/personal/PROGRAMMING/SYSTEM\ PROGRAMS/LINUX/μEmacs/include
LDFLAGS = -shared

TARGET = c_git.so
SRC = git_workflow.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) *.o
