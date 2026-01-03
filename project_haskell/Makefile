# Project Management Extension - Haskell + C Bridge

CC = gcc
GHC = ghc
CFLAGS = -O2 -fPIC -Wall

# GHC paths
GHC_LIBDIR := $(shell ghc --print-libdir)
GHC_RTS := $(GHC_LIBDIR)/x86_64-linux-ghc-9.6.6/rts-1.0.2

TARGET = project_haskell.so

all: $(TARGET)

# Build with statically linked RTS
$(TARGET): bridge.c Project.hs
	$(CC) $(CFLAGS) -c -o bridge.o bridge.c
	$(GHC) -O2 -shared -dynamic -fPIC -flink-rts \
		-optl-Wl,-rpath,$(GHC_LIBDIR)/x86_64-linux-ghc-9.6.6 \
		-o $@ Project.hs bridge.o

clean:
	rm -f $(TARGET) *.o *.hi *_stub.h

.PHONY: all clean
