// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package io

// #cgo rte_ring pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}
// #cgo rte_ring CFLAGS: -DRTE_RING_ENABLE -DSK_MSG_DISABLE
// #cgo sk_msg CFLAGS: -DRTE_RING_DISABLE -DSK_MSG_ENABLE
//
// #if defined(RTE_RING_ENABLE) || defined(SK_MSG_DISABLE)
// #include "io_rte_ring.c"
// #endif
// #if defined(RTE_RING_DISABLE) || defined(SK_MSG_ENABLE)
// #include "io_sk_msg.c"
// #endif
import "C"
