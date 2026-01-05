# Multiple Cursors Extension - Pascal Library + C Bridge
# Two-stage build: Pascal library + C wrapper linked with gcc

CC = gcc
FPC = fpc
CFLAGS = -O2 -fPIC -Wall

TARGET = pascal_multicursor.so

all: $(TARGET)

# Build Pascal as shared library first (libpascal_mc.so)
libpascal_mc.so: multicursor.pas
	$(FPC) -O2 -Cg -olibpascal_mc.so multicursor.pas

# Build final extension - bridge links to Pascal library
$(TARGET): bridge.c libpascal_mc.so
	$(CC) $(CFLAGS) -shared -o $@ bridge.c -L. -lpascal_mc -Wl,-rpath,'$$ORIGIN'

clean:
	rm -f $(TARGET) libpascal_mc.so *.o *.ppu link.res ppas.sh

.PHONY: all clean
