# Copyright 2022 University of California, Riverside
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

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

CLANG = clang
CLANGFLAGS = -g -O2
BPF_FLAGS = -target bpf

.PHONY: all shm_mgr gateway nf clean

all: bin shm_mgr gateway nf sockmap_manager adservice currencyservice \
		emailservice paymentservice shippingservice productcatalogservice \
		cartservice recommendationservice frontendservice checkoutservice \
		ebpf/sk_msg_kern.o

ebpf/sk_msg_kern.o: ebpf/sk_msg_kern.c
	@ $(CLANG) $(CLANGFLAGS) $(BPF_FLAGS) -c -o $@ $<

shm_mgr: bin/shm_mgr_rte_ring bin/shm_mgr_sk_msg

sockmap_manager: bin/sockmap_manager

bin/sockmap_manager: src/sockmap_manager.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

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

adservice: bin/nf_adservice_rte_ring bin/nf_adservice_sk_msg

bin/nf_adservice_rte_ring: src/io_rte_ring.o src/adservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_adservice_sk_msg: src/io_sk_msg.o src/adservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

currencyservice: bin/nf_currencyservice_rte_ring bin/nf_currencyservice_sk_msg

bin/nf_currencyservice_rte_ring: src/io_rte_ring.o src/currencyservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/nf_currencyservice_sk_msg: src/io_sk_msg.o src/currencyservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

emailservice: bin/nf_emailservice_rte_ring bin/nf_emailservice_sk_msg

bin/nf_emailservice_rte_ring: src/io_rte_ring.o src/emailservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_emailservice_sk_msg: src/io_sk_msg.o src/emailservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

paymentservice: bin/nf_paymentservice_rte_ring bin/nf_paymentservice_sk_msg

bin/nf_paymentservice_rte_ring: src/io_rte_ring.o src/paymentservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_paymentservice_sk_msg: src/io_sk_msg.o src/paymentservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

shippingservice: bin/nf_shippingservice_rte_ring bin/nf_shippingservice_sk_msg

bin/nf_shippingservice_rte_ring: src/io_rte_ring.o src/shippingservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_shippingservice_sk_msg: src/io_sk_msg.o src/shippingservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

-include $(patsubst %.o, %.d, $(wildcard src/*.o))

productcatalogservice: bin/nf_productcatalogservice_rte_ring bin/nf_productcatalogservice_sk_msg

bin/nf_productcatalogservice_rte_ring: src/io_rte_ring.o src/productcatalogservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/nf_productcatalogservice_sk_msg: src/io_sk_msg.o src/productcatalogservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

cartservice: bin/nf_cartservice_rte_ring bin/nf_cartservice_sk_msg

bin/nf_cartservice_rte_ring: src/io_rte_ring.o src/cartservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/nf_cartservice_sk_msg: src/io_sk_msg.o src/cartservice.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

recommendationservice: bin/nf_recommendationservice_rte_ring bin/nf_recommendationservice_sk_msg

bin/nf_recommendationservice_rte_ring: src/io_rte_ring.o src/recommendationservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_recommendationservice_sk_msg: src/io_sk_msg.o src/recommendationservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

frontendservice: bin/nf_frontendservice_rte_ring bin/nf_frontendservice_sk_msg

bin/nf_frontendservice_rte_ring: src/io_rte_ring.o src/frontendservice.o src/utility.o src/shm_rpc.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_frontendservice_sk_msg: src/io_sk_msg.o src/frontendservice.o src/utility.o src/shm_rpc.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

checkoutservice: bin/nf_checkoutservice_rte_ring bin/nf_checkoutservice_sk_msg

bin/nf_checkoutservice_rte_ring: src/io_rte_ring.o src/checkoutservice.o src/utility.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_checkoutservice_sk_msg: src/io_sk_msg.o src/checkoutservice.o src/utility.o
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
