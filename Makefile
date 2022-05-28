# SPDX-License-Identifier: MIT
# Copyright (c) 2022 University of California, Riverside

ifneq ($(shell pkg-config --exists libdpdk && echo 0), 0)
$(error "DPDK is not installed")
endif

CFLAGS = $(shell pkg-config --cflags libdpdk)
CFLAGS += -Iinclude -Isrc/include -MMD -MP -O3 -Wall -Werror

.PHONY: all shmmgr gateway node clean

all: lib shmmgr gateway node

shmmgr: lib/shmmgr_rte_ring.a lib/shmmgr_sk_msg.a

gateway: lib/gateway_rte_ring.a lib/gateway_sk_msg.a

node: lib/node_rte_ring.a lib/node_sk_msg.a

lib/shmmgr_rte_ring.a: src/shmmgr.o src/io_rte_ring.o
	@ echo "AR $@"
	@ ar -rcs $@ $^

lib/shmmgr_sk_msg.a: src/shmmgr.o src/io_sk_msg.o
	@ echo "AR $@"
	@ ar -rcs $@ $^

lib/gateway_rte_ring.a: src/gateway.o src/io_rte_ring.o
	@ echo "AR $@"
	@ ar -rcs $@ $^

lib/gateway_sk_msg.a: src/gateway.o src/io_sk_msg.o
	@ echo "AR $@"
	@ ar -rcs $@ $^

lib/node_rte_ring.a: src/node.o src/io_rte_ring.o
	@ echo "AR $@"
	@ ar -rcs $@ $^

lib/node_sk_msg.a: src/node.o src/io_sk_msg.o
	@ echo "AR $@"
	@ ar -rcs $@ $^

-include $(patsubst %.o, %.d, $(wildcard src/*.o))

%.o: %.c
	@ echo "CC $@"
	@ $(CC) -c $(CFLAGS) -o $@ $<

lib:
	@ mkdir -p $@

clean:
	@ echo "RM -r src/*.d src/*.o lib"
	@ $(RM) -r src/*.d src/*.o lib
