CC      := gcc
CFLAGS  := -Wall -Wextra -Werror -std=c99 -D_POSIX_C_SOURCE=200809L -g -Iinclude

SRCS    := src/main.c src/logger.c src/signal_manager.c src/process_manager.c
OBJS    := $(SRCS:src/%.c=build/%.o)

.PHONY: all clean test

all: watchdog worker

# Ana program: executable adi odevde "watchdog" olmali
watchdog: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Worker: watchdog ./worker yolundan exec ettigi icin ayni klasore derlenir
worker: workers/worker.c
	$(CC) $(CFLAGS) -o $@ $<

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

# Header degisince yeniden derle
$(OBJS): include/logger.h include/process_manager.h include/signal_manager.h

test: all
	./tests/test_scenarios.sh

clean:
	rm -rf build watchdog worker watchdog.log