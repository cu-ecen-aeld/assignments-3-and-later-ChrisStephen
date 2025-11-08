CROSS_COMPILE ?=
GCC ?= $(CROSS_COMPILE)gcc

.PHONY: all
all: aesdsocket

aesdsocket: aesdsocket.c
	$(GCC) aesdsocket.c -o aesdsocket

.PHONY: clean
clean:
	rm -f aesdsocket *.o
