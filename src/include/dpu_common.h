/*
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
*/


#ifndef DPU_COMMON_H_
#define DPU_COMMON_H_

#include <stdbool.h>

#include <doca_argp.h>
#include <doca_comm_channel.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_pe.h>

#define MAX_ARG_SIZE 128	      /* PCI address and file path maximum length */
#define CC_MAX_MSG_SIZE 4080	      /* Comm Channel message maximum size */
#define SERVER_NAME "dma copy server" /* Comm Channel service name */
#define NUM_DMA_TASKS (1)	      /* DMA tasks number */

/* Function to check if a given device is capable of executing some task */
typedef doca_error_t (*tasks_check)(struct doca_devinfo *);

/* DOCA core objects used by the samples / applications */
struct program_core_objects {
    struct doca_dev *dev;		    /* doca device */
    struct doca_mmap *src_mmap;	    /* doca mmap for source buffer */
    struct doca_mmap *dst_mmap;	    /* doca mmap for destination buffer */
    struct doca_buf_inventory *buf_inv; /* doca buffer inventory */
    struct doca_ctx *ctx;		    /* doca context */
    struct doca_pe *pe;		    /* doca progress engine */
};

enum dma_copy_mode {
    DMA_COPY_MODE_HOST, /* Run endpoint in Host */
    DMA_COPY_MODE_DPU   /* Run endpoint in DPU */
};

struct cc_msg_dma_direction {
    bool file_in_host;  /* Indicate where the source file is located */
    uint64_t file_size; /* File size in bytes */
};

struct cc_msg_dma_status {
    bool is_success; /* Indicate success or failure for last message sent */
};

struct dma_copy_cfg {
    enum dma_copy_mode mode;      /* Node running mode {host, dpu} */
    char file_path[MAX_ARG_SIZE]; /* File path to copy from (host) or path the save DMA result (dpu) */
    char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
    char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
    bool is_file_found_locally;				  /* Indicate DMA copy direction */
    uint64_t file_size;					  /* File size in bytes */
};

struct dma_copy_resources {
    struct program_core_objects *state; /* DOCA core objects */
    struct doca_dma *dma_ctx;	    /* DOCA DMA context */
};

/*
 * Register application arguments
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_dma_copy_params(void);

/*
 * Initiate Comm Channel
 *
 * @cfg [in]: Application configuration
 * @ep [out]: DOCA comm_channel endpoint
 * @dev [out]: DOCA device object to use
 * @dev_rep [out]: DOCA device representor object to use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_cc(struct dma_copy_cfg *cfg,
             struct doca_comm_channel_ep_t **ep,
             struct doca_dev **dev,
             struct doca_dev_rep **dev_rep);

/*
 * Destroy Comm Channel
 *
 * @ep [in]: Comm Channel DOCA endpoint
 * @peer [in]: Comm Channel DOCA address
 * @dev [in]: Comm Channel DOCA device
 * @dev_rep [in]: Comm Channel DOCA device representor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_cc(struct doca_comm_channel_ep_t *ep,
            struct doca_comm_channel_addr_t *peer,
            struct doca_dev *dev,
            struct doca_dev_rep *dev_rep);

/*
 * Open DOCA device for DMA operation
 *
 * @dev [in]: DOCA DMA capable device to open
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t open_dma_device(struct doca_dev **dev);

/*
 * Start DMA operation on the Host
 *
 * @dma_cfg [in]: App configuration structure
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t host_start_dma_copy(struct dma_copy_cfg *dma_cfg,
                 struct doca_comm_channel_ep_t *ep,
                 struct doca_comm_channel_addr_t **peer_addr);

/*
 * Start DMA operation on the DPU
 *
 * @dma_cfg [in]: App configuration structure
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t dpu_start_dma_copy(struct dma_copy_cfg *dma_cfg,
                struct doca_comm_channel_ep_t *ep,
                struct doca_comm_channel_addr_t **peer_addr);

/*
 * Open a DOCA device according to a given PCI address
 *
 * @pci_addr [in]: PCI address
 * @func [in]: pointer to a function that checks if the device have some task capabilities (Ignored if set to NULL)
 * @retval [out]: pointer to doca_dev struct, NULL if not found
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t open_doca_device_with_pci(const char *pci_addr, tasks_check func, struct doca_dev **retval);

/*
 * Open a DOCA device with a custom set of capabilities
 *
 * @func [in]: pointer to a function that checks if the device have some task capabilities
 * @retval [out]: pointer to doca_dev struct, NULL if not found
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t open_doca_device_with_capabilities(tasks_check func, struct doca_dev **retval);

/*
 * Open a DOCA device according to a given PCI address
 *
 * @local [in]: queries representors of the given local doca device
 * @filter [in]: bitflags filter to narrow the representors in the search
 * @pci_addr [in]: PCI address
 * @retval [out]: pointer to doca_dev_rep struct, NULL if not found
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t open_doca_device_rep_with_pci(struct doca_dev *local,
                       enum doca_devinfo_rep_filter filter,
                       const char *pci_addr,
                       struct doca_dev_rep **retval);

/*
 * Initialize a series of DOCA Core objects needed for the program's execution
 *
 * @state [in]: struct containing the set of initialized DOCA Core objects
 * @max_bufs [in]: maximum number of buffers for DOCA Inventory
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_core_objects(struct program_core_objects *state, uint32_t max_bufs);

/*
 * Request to stop context
 *
 * @pe [in]: DOCA progress engine
 * @ctx [in]: DOCA context added to the progress engine
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t request_stop_ctx(struct doca_pe *pe, struct doca_ctx *ctx);

/*
 * Cleanup the series of DOCA Core objects created by create_core_objects
 *
 * @state [in]: struct containing the set of initialized DOCA Core objects
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_core_objects(struct program_core_objects *state);

#endif /* DPU_COMMON_H_ */
