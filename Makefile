CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
PREFIX  ?= $(shell brew --prefix 2>/dev/null)
BUILD   := build

# Homebrew keeps headers and libs outside the default search path on macOS.
ifneq ($(PREFIX),)
CFLAGS  += -I$(PREFIX)/include
LDFLAGS += -L$(PREFIX)/lib
endif

all: $(BUILD)/prime_hunter $(BUILD)/agent $(BUILD)/agent_static \
     $(BUILD)/cunningham $(BUILD)/supervisor

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/prime_hunter: prime-hunter/prime_hunter.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lcurl

$(BUILD)/agent: agent/agent.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lcurl -lcjson

$(BUILD)/agent_static: agent/agent_static.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lcurl -lcjson

# One swarm worker. WORKER_ID/WORKER_COUNT are compile-time because BareMetal
# has no environment; scripts/swarm_deploy.sh rewrites them per worker.
$(BUILD)/cunningham: cunningham/cunningham.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lcurl

$(BUILD)/supervisor: supervisor/supervisor.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lcurl -lcjson

# Cross-checks the Miller-Rabin implementation by counting twin pairs below a
# limit; compare its output against a sieve (see README).
$(BUILD)/test_primes: prime-hunter/test_primes.c prime-hunter/prime_hunter.c | $(BUILD)
	$(CC) -O2 -std=c99 -o $@ $< $(LDFLAGS) -lcurl

check: $(BUILD)/test_primes
	./$(BUILD)/test_primes 1000000

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
