# Fuzzy File Finder Extension - Ada + C Bridge

CC = gcc
GNAT = gnatmake
CFLAGS = -O2 -fPIC -Wall
ADAFLAGS = -O2 -fPIC

TARGET = fuzzy_ada.so
C_OBJ = bridge.o

all: $(TARGET)

$(TARGET): $(C_OBJ) fuzzy.adb fuzzy.ads
	$(GNAT) -c $(ADAFLAGS) fuzzy.adb
	$(CC) -shared -o $@ $(C_OBJ) fuzzy.o -lgnat

$(C_OBJ): bridge.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o *.ali

.PHONY: all clean
