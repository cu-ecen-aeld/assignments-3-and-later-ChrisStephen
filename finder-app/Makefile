CROSS_COMPILE ?=
GCC ?= $(CROSS_COMPILE)gcc

.PHONY: all
all: writer

writer: writer.c
	$(GCC) writer.c -o writer

.PHONY: clean
clean:
	rm -f writer *.o
