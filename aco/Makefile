# The engine must build with no math library: -lm is deliberately absent.
# The test harness links it only for its own oracle.
#
# The binary name carries the instance. It must: a plain `aco` target is
# considered up to date when INSTANCE changes, so make silently reuses the
# previous instance's binary and every result is attributed to the wrong one.
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra -std=c99
INSTANCE ?= berlin52
BIN      := build/aco-$(INSTANCE)

all: $(BIN)

build:
	@mkdir -p build

$(BIN): aco.c instances/$(INSTANCE).h | build
	$(CC) $(CFLAGS) -DACO_INSTANCE_HEADER='"instances/$(INSTANCE).h"' -o $@ aco.c

build/test_aco: test_aco.c aco.c instances/tiny5.h | build
	$(CC) $(CFLAGS) -DACO_INSTANCE_HEADER='"instances/tiny5.h"' -o $@ test_aco.c -lm

check: build/test_aco
	./build/test_aco

clean:
	rm -rf build aco test_aco

.PHONY: all check clean
