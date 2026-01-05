# Fuzzy File Finder Extension - Ada + C Bridge
#
# Uses gnatbind to generate proper elaboration code (adainit/adafinal)
# Without this, Ada's secondary stack is uninitialized and causes crashes.

CC = gcc
GNAT = gnatmake
GNATBIND = gnatbind
CFLAGS = -O2 -fPIC -Wall
ADAFLAGS = -O2 -fPIC

TARGET = ada_fuzzy.so
C_OBJ = bridge.o

all: $(TARGET)

# Build shared library with proper Ada elaboration
$(TARGET): $(C_OBJ) fuzzy.adb fuzzy.ads
	# Compile Ada code
	$(GNAT) -c $(ADAFLAGS) fuzzy.adb
	# Generate binding/elaboration code (creates b~fuzzy.adb)
	$(GNATBIND) -n -shared fuzzy
	# Compile the binding file
	$(CC) $(CFLAGS) -c b~fuzzy.adb
	# Link everything together
	$(CC) -shared -o $@ $(C_OBJ) fuzzy.o b~fuzzy.o -lgnat

$(C_OBJ): bridge.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o *.ali b~fuzzy.ad?

.PHONY: all clean
