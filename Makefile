# SPDX-License-Identifier: MIT
# Copyright (c) 2022 University of California, Riverside

ifneq ($(shell pkg-config --exists libconfig && echo 0), 0)
$(error "libconfig is not installed")
endif

ifneq ($(shell pkg-config --exists libdpdk && echo 0), 0)
$(error "DPDK is not installed")
endif

CFLAGS = $(shell pkg-config --cflags libconfig libdpdk)
LDFLAGS = $(shell pkg-config --libs-only-L libconfig libdpdk)
LDLIBS = $(shell pkg-config --libs-only-l libconfig libdpdk)

CFLAGS += -Isrc/include -MMD -MP -O3 -Wall -Werror
LDLIBS += -lbpf -lm -pthread

# export GO111MODULE=auto

.PHONY: all shm_mgr gateway nf go_nf clean

all: bin shm_mgr gateway nf go_nf

shm_mgr: bin/shm_mgr_rte_ring bin/shm_mgr_sk_msg

# mod-download: ## Downloads the Go module.
# 	@echo "==> Downloading Go module"
# 	@ cd go/adservice/; go mod download; cd ../../
# 	@ cd go/cartservice/; go mod download; cd ../../
# .PHONY: mod-download

bin/shm_mgr_rte_ring: src/io_rte_ring.o src/shm_mgr.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/shm_mgr_sk_msg: src/io_sk_msg.o src/shm_mgr.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

gateway: bin/gateway_rte_ring bin/gateway_sk_msg

bin/gateway_rte_ring: src/io_rte_ring.o src/gateway.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/gateway_sk_msg: src/io_sk_msg.o src/gateway.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

nf: bin/nf_rte_ring bin/nf_sk_msg

bin/nf_rte_ring: src/io_rte_ring.o src/nf.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_sk_msg: src/io_sk_msg.o src/nf.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

go_nf: bin/go_nf_rte_ring bin/go_nf_sk_msg # bin/adservice_rte_ring bin/cartservice_rte_ring

bin/go_nf_rte_ring: go/nf.go src/io_rte_ring.o
	@ echo "GO BUILD $@"
	@ CGO_CFLAGS_ALLOW=".*" go build -o $@ -tags="rte_ring" $<

bin/go_nf_sk_msg: go/nf.go src/io_sk_msg.o
	@ echo "GO BUILD $@"
	@ CGO_CFLAGS_ALLOW=".*" go build -o $@ -tags="sk_msg" $<

# bin/adservice_rte_ring: go/adservice/adservice.go src/io_rte_ring.o
# 	@ echo "GO BUILD $@"
# 	@ cd go/adservice/; CGO_CFLAGS_ALLOW=".*" go build -o $@ -tags="rte_ring" adservice.go; cd ../../

# bin/cartservice_rte_ring: go/cartservice/cartservice.go src/io_rte_ring.o
# 	@ echo "GO BUILD $@"
# 	@ cd go/cartservice/; CGO_CFLAGS_ALLOW=".*" go build -o $@ -tags="rte_ring" cartservice.go

-include $(patsubst %.o, %.d, $(wildcard src/*.o))

%.o: %.c
	@ echo "CC $@"
	@ $(CC) -c $(CFLAGS) -o $@ $<

bin:
	@ mkdir -p $@

clean:
	@ echo "RM -r src/*.d src/*.o bin"
	@ $(RM) -r src/*.d src/*.o bin
