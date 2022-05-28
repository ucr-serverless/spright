# SPDX-License-Identifier: MIT
# Copyright (c) 2022 University of California, Riverside

ifneq ($(shell pkg-config --exists libdpdk && echo 0), 0)
$(error "DPDK is not installed")
endif

CFLAGS = $(shell pkg-config --cflags libconfig libdpdk)
CFLAGS += -fPIC -Iinclude -Isrc/include -MMD -MP -O3 -Wall -Werror

LDFLAGS = $(shell pkg-config --libs-only-L libconfig libdpdk)
LDLIBS = $(shell pkg-config --libs-only-l libconfig libdpdk)
LDLIBS += -lbpf

.PHONY: all shmmgr gateway node clean

all: lib shmmgr gateway node

shmmgr: lib/shmmgr_rte_ring.so lib/shmmgr_sk_msg.so

gateway: lib/gateway_rte_ring.so lib/gateway_sk_msg.so

node: lib/node_rte_ring.so lib/node_sk_msg.so

lib/shmmgr_rte_ring.so: src/shmmgr.o src/io_rte_ring.o
	@ echo "CC $@"
	@ $(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

lib/shmmgr_sk_msg.so: src/shmmgr.o src/io_sk_msg.o
	@ echo "CC $@"
	@ $(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

lib/gateway_rte_ring.so: src/gateway.o src/io_rte_ring.o
	@ echo "CC $@"
	@ $(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

lib/gateway_sk_msg.so: src/gateway.o src/io_sk_msg.o
	@ echo "CC $@"
	@ $(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

lib/node_rte_ring.so: src/node.o src/io_rte_ring.o
	@ echo "CC $@"
	@ $(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

lib/node_sk_msg.so: src/node.o src/io_sk_msg.o
	@ echo "CC $@"
	@ $(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

-include $(patsubst %.o, %.d, $(wildcard src/*.o))

%.o: %.c
	@ echo "CC $@"
	@ $(CC) -c $(CFLAGS) -o $@ $<

lib:
	@ mkdir -p $@

clean:
	@ echo "RM -r src/*.d src/*.o lib"
	@ $(RM) -r src/*.d src/*.o lib
