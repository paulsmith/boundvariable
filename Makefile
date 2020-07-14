CFLAGS ?= -Wall -pedantic -std=c11 -O0 -g

um-32: um-32.c
	$(CC) $(CFLAGS) -o $@ $<
