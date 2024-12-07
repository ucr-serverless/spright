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
LDFLAGS = $(shell pkg-config --libs-only-L libconfig libdpdk) -L/usr/lib64
LDLIBS = $(shell pkg-config --libs-only-l libconfig libdpdk)

CFLAGS += -Isrc/include -Isrc/cstl/inc -Isrc/log -MMD \
		  -MP -O3 -Wall -Werror -DLOG_USE_COLOR
LDLIBS += -lbpf -lm -pthread -luuid

CLANG = clang
CLANGFLAGS = -g -O2
BPF_FLAGS = -target bpf

COMMON_OBJS = src/log/log.o src/utility.o src/timer.o src/io_helper.o src/common.o

.PHONY: all shm_mgr gateway sk_gateway nf sk_nf clean

all: bin shm_mgr gateway sk_gateway nf sk_nf sockmap_manager adservice currencyservice \
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

bin/shm_mgr_rte_ring: src/io_rte_ring.o src/shm_mgr.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/shm_mgr_sk_msg: src/io_sk_msg.o src/shm_mgr.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

gateway: bin/gateway_rte_ring bin/gateway_sk_msg

bin/gateway_rte_ring: src/io_rte_ring.o src/gateway.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/gateway_sk_msg: src/io_sk_msg.o src/gateway.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

sk_gateway: bin/sk_gateway_sk_msg

bin/sk_gateway_sk_msg: src/io_socket.o src/gateway.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

nf: bin/nf_rte_ring bin/nf_sk_msg

bin/nf_rte_ring: src/io_rte_ring.o src/nf.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_sk_msg: src/io_sk_msg.o src/nf.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

sk_nf: bin/sk_nf_sk_msg

bin/sk_nf_sk_msg: src/io_socket.o src/nf.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

adservice: bin/nf_adservice_rte_ring bin/nf_adservice_sk_msg

bin/nf_adservice_rte_ring: src/io_rte_ring.o src/online_boutique/adservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_adservice_sk_msg: src/io_sk_msg.o src/online_boutique/adservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

currencyservice: bin/nf_currencyservice_rte_ring bin/nf_currencyservice_sk_msg

bin/nf_currencyservice_rte_ring: src/io_rte_ring.o src/online_boutique/currencyservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/nf_currencyservice_sk_msg: src/io_sk_msg.o src/online_boutique/currencyservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

emailservice: bin/nf_emailservice_rte_ring bin/nf_emailservice_sk_msg

bin/nf_emailservice_rte_ring: src/io_rte_ring.o src/online_boutique/emailservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_emailservice_sk_msg: src/io_sk_msg.o src/online_boutique/emailservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

paymentservice: bin/nf_paymentservice_rte_ring bin/nf_paymentservice_sk_msg

bin/nf_paymentservice_rte_ring: src/io_rte_ring.o src/online_boutique/paymentservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_paymentservice_sk_msg: src/io_sk_msg.o src/online_boutique/paymentservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

shippingservice: bin/nf_shippingservice_rte_ring bin/nf_shippingservice_sk_msg

bin/nf_shippingservice_rte_ring: src/io_rte_ring.o src/online_boutique/shippingservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_shippingservice_sk_msg: src/io_sk_msg.o src/online_boutique/shippingservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

-include $(patsubst %.o, %.d, $(wildcard src/*.o))

productcatalogservice: bin/nf_productcatalogservice_rte_ring bin/nf_productcatalogservice_sk_msg

bin/nf_productcatalogservice_rte_ring: src/io_rte_ring.o src/online_boutique/productcatalogservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/nf_productcatalogservice_sk_msg: src/io_sk_msg.o src/online_boutique/productcatalogservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

cartservice: bin/nf_cartservice_rte_ring bin/nf_cartservice_sk_msg

bin/nf_cartservice_rte_ring: src/io_rte_ring.o src/online_boutique/cartservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

bin/nf_cartservice_sk_msg: src/io_sk_msg.o src/online_boutique/cartservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) ./src/cstl/src/libclib.a

recommendationservice: bin/nf_recommendationservice_rte_ring bin/nf_recommendationservice_sk_msg

bin/nf_recommendationservice_rte_ring: src/io_rte_ring.o src/online_boutique/recommendationservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_recommendationservice_sk_msg: src/io_sk_msg.o src/online_boutique/recommendationservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

frontendservice: bin/nf_frontendservice_rte_ring bin/nf_frontendservice_sk_msg

bin/nf_frontendservice_rte_ring: src/io_rte_ring.o src/online_boutique/frontendservice.o $(COMMON_OBJS) src/shm_rpc.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_frontendservice_sk_msg: src/io_sk_msg.o src/online_boutique/frontendservice.o $(COMMON_OBJS) src/shm_rpc.o
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

checkoutservice: bin/nf_checkoutservice_rte_ring bin/nf_checkoutservice_sk_msg

bin/nf_checkoutservice_rte_ring: src/io_rte_ring.o src/online_boutique/checkoutservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

bin/nf_checkoutservice_sk_msg: src/io_sk_msg.o src/online_boutique/checkoutservice.o $(COMMON_OBJS)
	@ echo "CC $@"
	@ $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
-include $(patsubst %.o, %.d, $(wildcard src/*.o))

%.o: %.c
	@ echo "CC $@"
	@ $(CC) -c $(CFLAGS) -o $@ $<

bin:
	@ mkdir -p $@

clean:
	@ echo "RM -r src/*.d src/*.o src/*/*.o src/*/*.d bin"
	@ $(RM) -r src/*.d src/*.o src/*/*.o src/*/*.d bin

format:
	@ clang-format -i src/*.c src/include/*.h src/online_boutique/*.c src/log/*.c src/log/*.h
