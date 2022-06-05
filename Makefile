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

CFLAGS += -Isrc/include -Isrc/cstl/inc -MMD -MP -O3 -Wall -Werror
LDLIBS += -lbpf -lm -pthread -luuid

.PHONY: all shm_mgr gateway nf clean

all: bin shm_mgr gateway nf adservice currencyservice emailservice paymentservice shippingservice productcatalogservice cartservice recommendationservice frontend checkoutservice

shm_mgr: bin/shm_mgr_rte_ring bin/shm_mgr_sk_msg

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

adservice: bin/adservice_rte_ring bin/adservice_sk_msg

bin/adservice_rte_ring: src/io_rte_ring.o src/adservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/adservice_sk_msg: src/io_sk_msg.o src/adservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

currencyservice: bin/currencyservice_rte_ring bin/currencyservice_sk_msg

bin/currencyservice_rte_ring: src/io_rte_ring.o src/currencyservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/currencyservice_sk_msg: src/io_sk_msg.o src/currencyservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

emailservice: bin/emailservice_rte_ring bin/emailservice_sk_msg

bin/emailservice_rte_ring: src/io_rte_ring.o src/emailservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/emailservice_sk_msg: src/io_sk_msg.o src/emailservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

paymentservice: bin/paymentservice_rte_ring bin/paymentservice_sk_msg

bin/paymentservice_rte_ring: src/io_rte_ring.o src/paymentservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/paymentservice_sk_msg: src/io_sk_msg.o src/paymentservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

shippingservice: bin/shippingservice_rte_ring bin/shippingservice_sk_msg

bin/shippingservice_rte_ring: src/io_rte_ring.o src/shippingservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/shippingservice_sk_msg: src/io_sk_msg.o src/shippingservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

-include $(patsubst %.o, %.d, $(wildcard src/*.o))

productcatalogservice: bin/productcatalogservice_rte_ring bin/productcatalogservice_sk_msg

bin/productcatalogservice_rte_ring: src/io_rte_ring.o src/productcatalogservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/productcatalogservice_sk_msg: src/io_sk_msg.o src/productcatalogservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

cartservice: bin/cartservice_rte_ring bin/cartservice_sk_msg

bin/cartservice_rte_ring: src/io_rte_ring.o src/cartservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/cartservice_sk_msg: src/io_sk_msg.o src/cartservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

recommendationservice: bin/recommendationservice_rte_ring bin/recommendationservice_sk_msg

bin/recommendationservice_rte_ring: src/io_rte_ring.o src/recommendationservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/recommendationservice_sk_msg: src/io_sk_msg.o src/recommendationservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

frontend: bin/frontend_rte_ring bin/frontend_sk_msg

bin/frontend_rte_ring: src/io_rte_ring.o src/frontend.o src/utility.o src/shm_rpc.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/frontend_sk_msg: src/io_sk_msg.o src/frontend.o src/utility.o src/shm_rpc.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

checkoutservice: bin/checkoutservice_rte_ring bin/checkoutservice_sk_msg

bin/checkoutservice_rte_ring: src/io_rte_ring.o src/checkoutservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/checkoutservice_sk_msg: src/io_sk_msg.o src/checkoutservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

%.o: %.c
	@ echo "CC $@"
	@ $(CC) -c $(CFLAGS) -o $@ $<

bin:
	@ mkdir -p $@

clean:
	@ echo "RM -r src/*.d src/*.o bin"
	@ $(RM) -r src/*.d src/*.o bin
