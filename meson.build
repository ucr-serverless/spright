# Copyright 2024 University of California, Riverside
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

project('spright', 'c',
  license: 'Apache-2.0',
  default_options: ['warning_level=3', 'optimization=3', 'buildtype=release'],
  meson_version: '>= 0.61.2'
)

# Disable compiler warnings
add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments('-Wno-error=sign-compare', language: 'c')

# Check for deps
libconfig_dep = dependency('libconfig', required: true)
dpdk_dep = dependency('libdpdk', required: true)

inc_dirs = include_directories('src/include', 'src/cstl/inc', 'src/log')

# Define compiler and linker flags
cflags = ['-MMD', '-MP', '-O3', '-Wall', '-Werror', '-DLOG_USE_COLOR']
ldflags = ['-L/usr/lib64']
ldlibs = ['-lbpf', '-lm', '-pthread', '-luuid']

# src files for common objects
common_src = [
  'src/log/log.c',
  'src/utility.c',
  'src/timer.c',
  'src/io_helper.c',
  'src/common.c'
]

common_objs = static_library('common', common_src,
  include_directories: inc_dirs,
  c_args: cflags,
  link_args: ldflags + ldlibs,
  dependencies: [libconfig_dep, dpdk_dep]
)

# Include the cstl directory in root build
subdir('src/cstl')

# Binaries definitions
# TODO: use loops to define binaries
binaries = [
  ['sockmap_manager', ['src/sockmap_manager.c']],
  ['shm_mgr_rte_ring', ['src/io_rte_ring.c', 'src/shm_mgr.c']],
  ['shm_mgr_sk_msg', ['src/io_sk_msg.c', 'src/shm_mgr.c']],
  ['gateway_rte_ring', ['src/io_rte_ring.c', 'src/gateway.c']],
  ['gateway_sk_msg', ['src/io_sk_msg.c', 'src/gateway.c']],
  ['nf_rte_ring', ['src/io_rte_ring.c', 'src/nf.c']],
  ['nf_sk_msg', ['src/io_sk_msg.c', 'src/nf.c']],
  ['nf_adservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/adservice.c']],
  ['nf_adservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/adservice.c']],
  ['nf_currencyservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/currencyservice.c']],
  ['nf_currencyservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/currencyservice.c']],
  ['nf_emailservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/emailservice.c']],
  ['nf_emailservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/emailservice.c']],
  ['nf_paymentservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/paymentservice.c']],
  ['nf_paymentservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/paymentservice.c']],
  ['nf_shippingservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/shippingservice.c']],
  ['nf_shippingservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/shippingservice.c']],
  ['nf_productcatalogservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/productcatalogservice.c']],
  ['nf_productcatalogservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/productcatalogservice.c']],
  ['nf_cartservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/cartservice.c']],
  ['nf_cartservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/cartservice.c']],
  ['nf_recommendationservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/recommendationservice.c']],
  ['nf_recommendationservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/recommendationservice.c']],
  ['nf_frontendservice_rte_ring', ['src/io_rte_ring.c', 'src/shm_rpc.c', 'src/online_boutique/frontendservice.c']],
  ['nf_frontendservice_sk_msg', ['src/io_sk_msg.c', 'src/shm_rpc.c', 'src/online_boutique/frontendservice.c']],
  ['nf_checkoutservice_rte_ring', ['src/io_rte_ring.c', 'src/online_boutique/checkoutservice.c']],
  ['nf_checkoutservice_sk_msg', ['src/io_sk_msg.c', 'src/online_boutique/checkoutservice.c']],
]

# Generate bins
foreach bin : binaries
  executable(bin[0],
    sources: bin[1] + common_src,
    include_directories: inc_dirs,
    c_args: cflags,
    link_args: ldflags + ldlibs,
    dependencies: [libconfig_dep, dpdk_dep],
    link_with: [common_objs, libclib_dep],
    install: false
  )
endforeach
