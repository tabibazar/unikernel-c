# The engine must build with no math library: -lm is deliberately absent.
# The test harness links it only for its own oracle.
#
# The binary name carries EVERY compile-time setting that changes behaviour --
# the instance and the distance-cache flag. It must: make judges a target by
# timestamp, not by the flags it was built with, so a shared name lets a stale
# binary satisfy a build with different -D options and every result is then
# attributed to a configuration that never ran. This has bitten twice.
CC         ?= cc
CFLAGS     ?= -O2 -Wall -Wextra -std=c99
INSTANCE   ?= berlin52
DIST_CACHE ?= 1
LOCAL_SEARCH ?= 1
BIN        := build/aco-$(INSTANCE)-c$(DIST_CACHE)-l$(LOCAL_SEARCH)

all: $(BIN)
	@echo "$(BIN)"

build:
	@mkdir -p build

$(BIN): aco.c instances/$(INSTANCE).h | build
	$(CC) $(CFLAGS) -DACO_DIST_CACHE=$(DIST_CACHE) -DACO_LOCAL_SEARCH=$(LOCAL_SEARCH) \
	      -DACO_INSTANCE_HEADER='"instances/$(INSTANCE).h"' -o $@ aco.c

build/test_aco: test_aco.c aco.c instances/tiny5.h | build
	$(CC) $(CFLAGS) -DACO_INSTANCE_HEADER='"instances/tiny5.h"' -o $@ test_aco.c -lm

check: build/test_aco
	./build/test_aco

clean:
	rm -rf build

.PHONY: all check clean
