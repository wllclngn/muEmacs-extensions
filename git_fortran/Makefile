# Git Integration Extension - Fortran + C Bridge
# Builds a shared library for uEmacs

CC = gcc
FC = gfortran
CFLAGS = -O2 -fPIC -Wall
FFLAGS = -O2 -fPIC -ffree-form -std=f2008

TARGET = git_fortran.so
OBJS = bridge.o git_ext.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(FC) -shared -o $@ $(OBJS)

bridge.o: bridge.c
	$(CC) $(CFLAGS) -c -o $@ $<

git_ext.o: git_ext.f90
	$(FC) $(FFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS) *.mod

.PHONY: all clean
