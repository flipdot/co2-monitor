all: monitor
monitor: monitor.c

.PHONY: clean
clean:
	-rm monitor

CFLAGS=-std=c11 -Wall -Wextra -pedantic -ludev