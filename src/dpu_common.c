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


#include <stdint.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_dma.h>
#include <doca_mmap.h>

#include "pack.h"
#include "utils.h"

#include "dpu_common.h"

#define CC_MAX_QUEUE_SIZE 10	   /* Max number of messages on Comm Channel queue */
#define SLEEP_IN_NANOS (10 * 1000) /* Sample the task every 10 microseconds  */
#define STATUS_SUCCESS true	   /* Successful status */
#define STATUS_FAILURE false	   /* Unsuccessful status */

DOCA_LOG_REGISTER(DPU_COMMON)

static void spright_cfg_print(struct spright_cfg_s *spright_cfg)
{
    uint8_t i;
    uint8_t j;

    printf("Name: %s\n", spright_cfg->name);

    printf("Number of Tenants: %d\n", spright_cfg->n_tenants);
    printf("Tenants:\n");
    for (i = 0; i < spright_cfg->n_tenants; i++)
    {
        printf("\tID: %hhu\n", i);
        printf("\tWeight: %d\n", spright_cfg->tenants[i].weight);
        printf("\n");
    }

    printf("Number of NFs: %hhu\n", spright_cfg->n_nfs);
    printf("NFs:\n");
    for (i = 0; i < spright_cfg->n_nfs; i++)
    {
        printf("\tID: %hhu\n", i + 1);
        printf("\tName: %s\n", spright_cfg->nf[i].name);
        printf("\tNumber of Threads: %hhu\n", spright_cfg->nf[i].n_threads);
        printf("\tParams:\n");
        printf("\t\tmemory_mb: %hhu\n", spright_cfg->nf[i].param.memory_mb);
        printf("\t\tsleep_ns: %u\n", spright_cfg->nf[i].param.sleep_ns);
        printf("\t\tcompute: %u\n", spright_cfg->nf[i].param.compute);
        printf("\tNode: %u\n", spright_cfg->nf[i].node);
        printf("\n");
    }

    printf("Number of Routes: %hhu\n", spright_cfg->n_routes);
    printf("Routes:\n");
    for (i = 0; i < spright_cfg->n_routes; i++)
    {
        printf("\tID: %hhu\n", i);
        printf("\tName: %s\n", spright_cfg->route[i].name);
        printf("\tLength = %hhu\n", spright_cfg->route[i].length);
        if (spright_cfg->route[i].length > 0)
        {
            printf("\tHops = [");
            for (j = 0; j < spright_cfg->route[i].length; j++)
            {
                printf("%hhu ", spright_cfg->route[i].hop[j]);
            }
            printf("\b]\n");
        }
        printf("\n");
    }

    printf("Number of Nodes: %hhu\n", spright_cfg->n_nodes);
    printf("Local Node Index: %u\n", spright_cfg->local_node_idx);
    printf("Nodes:\n");
    for (i = 0; i < spright_cfg->n_nodes; i++)
    {
        printf("\tID: %hhu\n", i);
        printf("\tHostname: %s\n", spright_cfg->nodes[i].hostname);
        printf("\tIP Address: %s\n", spright_cfg->nodes[i].ip_address);
        printf("\tPort = %u\n", spright_cfg->nodes[i].port);
        printf("\n");
    }

    // print_rt_table();
}

/*
 * Get DOCA DMA maximum buffer size allowed
 *
 * @resources [in]: DOCA DMA resources pointer
 * @max_buf_size [out]: Maximum buffer size allowed
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t get_dma_max_buf_size(struct dma_copy_resources *resources, uint64_t *max_buf_size)
{
    struct doca_devinfo *dma_dev_info = doca_dev_as_devinfo(resources->state->dev);
    doca_error_t result;

    result = doca_dma_cap_task_memcpy_get_max_buf_size(dma_dev_info, max_buf_size);
    if (result != DOCA_SUCCESS)
        DOCA_LOG_ERR("Failed to retrieve maximum buffer size allowed from DOCA DMA device");
    else
        DOCA_LOG_DBG("DOCA DMA device supports maximum buffer size of %" PRIu64 " bytes", *max_buf_size);

    return result;
}

/*
 * Validate file size
 *
 * @file_path [in]: File to validate
 * @file_size [out]: File size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t validate_file_size(const char *file_path, uint64_t *file_size)
{
    FILE *fp;
    long size;

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        DOCA_LOG_ERR("Failed to open %s", file_path);
        return DOCA_ERROR_IO_FAILED;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        DOCA_LOG_ERR("Failed to calculate file size");
        fclose(fp);
        return DOCA_ERROR_IO_FAILED;
    }

    size = ftell(fp);
    if (size == -1) {
        DOCA_LOG_ERR("Failed to calculate file size");
        fclose(fp);
        return DOCA_ERROR_IO_FAILED;
    }

    fclose(fp);

    DOCA_LOG_INFO("The file size is %ld", size);

    *file_size = size;

    return DOCA_SUCCESS;
}

/*
 * ARGP validation Callback - check if input file exists
 *
 * @config [in]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t args_validation_callback(void *config)
{
    struct dma_copy_cfg *cfg = (struct dma_copy_cfg *)config;

    if (access(cfg->file_path, F_OK | R_OK) == 0) {
        cfg->is_file_found_locally = true;
        return validate_file_size(cfg->file_path, &cfg->file_size);
    }

    return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Comm Channel DOCA device PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t dev_pci_addr_callback(void *param, void *config)
{
    struct dma_copy_cfg *cfg = (struct dma_copy_cfg *)config;
    const char *dev_pci_addr = (char *)param;

    if (strnlen(dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE) == DOCA_DEVINFO_PCI_ADDR_SIZE) {
        DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d",
                 DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
        return DOCA_ERROR_INVALID_VALUE;
    }

    strlcpy(cfg->cc_dev_pci_addr, dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE);

    return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t file_path_callback(void *param, void *config)
{
    struct dma_copy_cfg *cfg = (struct dma_copy_cfg *)config;
    char *file_path = (char *)param;
    int file_path_len = strnlen(file_path, MAX_ARG_SIZE);

    if (file_path_len == MAX_ARG_SIZE) {
        DOCA_LOG_ERR("Entered file path exceeded buffer size - MAX=%d", MAX_ARG_SIZE - 1);
        return DOCA_ERROR_INVALID_VALUE;
    }

    strlcpy(cfg->file_path, file_path, MAX_ARG_SIZE);

    return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Comm Channel DOCA device representor PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rep_pci_addr_callback(void *param, void *config)
{
    struct dma_copy_cfg *cfg = (struct dma_copy_cfg *)config;
    const char *rep_pci_addr = (char *)param;

    if (cfg->mode == DMA_COPY_MODE_DPU) {
        if (strnlen(rep_pci_addr, DOCA_DEVINFO_REP_PCI_ADDR_SIZE) == DOCA_DEVINFO_REP_PCI_ADDR_SIZE) {
            DOCA_LOG_ERR("Entered device representor PCI address exceeding the maximum size of %d",
                     DOCA_DEVINFO_REP_PCI_ADDR_SIZE - 1);
            return DOCA_ERROR_INVALID_VALUE;
        }

        strlcpy(cfg->cc_dev_rep_pci_addr, rep_pci_addr, DOCA_DEVINFO_REP_PCI_ADDR_SIZE);
    }

    return DOCA_SUCCESS;
}

/*
 * Wait for status message
 *
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t wait_for_successful_status_msg(struct doca_comm_channel_ep_t *ep,
                           struct doca_comm_channel_addr_t **peer_addr)
{
    struct cc_msg_dma_status msg_status;
    doca_error_t result;
    size_t msg_len, status_msg_len = sizeof(struct cc_msg_dma_status);
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };

    msg_len = status_msg_len;
    while ((result = doca_comm_channel_ep_recvfrom(ep,
                               (void *)&msg_status,
                               &msg_len,
                               DOCA_CC_MSG_FLAG_NONE,
                               peer_addr)) == DOCA_ERROR_AGAIN) {
        nanosleep(&ts, &ts);
        msg_len = status_msg_len;
    }
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Status message was not received: %s", doca_error_get_descr(result));
        return result;
    }

    if (!msg_status.is_success) {
        DOCA_LOG_ERR("Failure status received");
        return DOCA_ERROR_INVALID_VALUE;
    }

    return DOCA_SUCCESS;
}

/*
 * Send status message
 *
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @status [in]: Status to send
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t send_status_msg(struct doca_comm_channel_ep_t *ep,
                    struct doca_comm_channel_addr_t **peer_addr,
                    bool status)
{
    struct cc_msg_dma_status status_msg;
    doca_error_t result;
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };

    status_msg.is_success = status;

    while ((result = doca_comm_channel_ep_sendto(ep,
                             &status_msg,
                             sizeof(struct cc_msg_dma_status),
                             DOCA_CC_MSG_FLAG_NONE,
                             *peer_addr)) == DOCA_ERROR_AGAIN)
        nanosleep(&ts, &ts);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to send status message: %s", doca_error_get_descr(result));
        return result;
    }

    return DOCA_SUCCESS;
}

/*
 * Save remote buffer information into a file
 *
 * @cfg [in]: Application configuration
 * @buffer [in]: Buffer to read information from
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t save_buffer_into_a_file(struct dma_copy_cfg *cfg, const char *buffer)
{
    FILE *fp;

    fp = fopen(cfg->file_path, "w");
    if (fp == NULL) {
        DOCA_LOG_ERR("Failed to create the DMA copy file");
        return DOCA_ERROR_IO_FAILED;
    }

    if (fwrite(buffer, 1, cfg->file_size, fp) != cfg->file_size) {
        DOCA_LOG_ERR("Failed to write full content into the output file");
        fclose(fp);
        return DOCA_ERROR_IO_FAILED;
    }

    fclose(fp);

    return DOCA_SUCCESS;
}

/*
 * Fill local buffer with file content
 *
 * @cfg [in]: Application configuration
 * @buffer [out]: Buffer to save information into
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t fill_buffer_with_file_content(struct dma_copy_cfg *cfg, char *buffer)
{
    FILE *fp;

    fp = fopen(cfg->file_path, "r");
    if (fp == NULL) {
        DOCA_LOG_ERR("Failed to open %s", cfg->file_path);
        return DOCA_ERROR_IO_FAILED;
    }

    /* Read file content and store it in the local buffer which will be exported */
    if (fread(buffer, 1, cfg->file_size, fp) != cfg->file_size) {
        DOCA_LOG_ERR("Failed to read content from file: %s", cfg->file_path);
        fclose(fp);
        return DOCA_ERROR_IO_FAILED;
    }
    fclose(fp);

    return DOCA_SUCCESS;
}

/*
 * Host side function for file size and location negotiation
 *
 * @cfg [in]: Application configuration
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t host_negotiate_dma_direction_and_size(struct dma_copy_cfg *cfg,
                              struct doca_comm_channel_ep_t *ep,
                              struct doca_comm_channel_addr_t **peer_addr)
{
    struct cc_msg_dma_direction host_dma_direction = {0};
    struct cc_msg_dma_direction dpu_dma_direction = {0};
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };
    doca_error_t result;
    size_t msg_len;

    result = doca_comm_channel_ep_connect(ep, SERVER_NAME, peer_addr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to establish a connection with the DPU: %s", doca_error_get_descr(result));
        return result;
    }

    while ((result = doca_comm_channel_peer_addr_update_info(*peer_addr)) == DOCA_ERROR_CONNECTION_INPROGRESS)
        nanosleep(&ts, &ts);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to validate the connection with the DPU: %s", doca_error_get_descr(result));
        return result;
    }

    DOCA_LOG_INFO("Connection to DPU was established successfully");

    /* First byte indicates if file is located on Host, other 4 bytes determine file size */
    if (cfg->is_file_found_locally) {
        DOCA_LOG_INFO("File was found locally, it will be DMA copied to the DPU");
        host_dma_direction.file_size = htonq(cfg->file_size);
        host_dma_direction.file_in_host = true;
    } else {
        DOCA_LOG_INFO("File was not found locally, it will be DMA copied from the DPU");
        host_dma_direction.file_in_host = false;
    }

    while ((result = doca_comm_channel_ep_sendto(ep,
                             &host_dma_direction,
                             sizeof(host_dma_direction),
                             DOCA_CC_MSG_FLAG_NONE,
                             *peer_addr)) == DOCA_ERROR_AGAIN)
        nanosleep(&ts, &ts);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to send negotiation buffer to DPU: %s", doca_error_get_descr(result));
        return result;
    }

    DOCA_LOG_INFO("Waiting for DPU to send negotiation message");

    msg_len = sizeof(struct cc_msg_dma_direction);
    while ((result = doca_comm_channel_ep_recvfrom(ep,
                               (void *)&dpu_dma_direction,
                               &msg_len,
                               DOCA_CC_MSG_FLAG_NONE,
                               peer_addr)) == DOCA_ERROR_AGAIN) {
        nanosleep(&ts, &ts);
        msg_len = sizeof(struct cc_msg_dma_direction);
    }
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Negotiation message was not received: %s", doca_error_get_descr(result));
        return result;
    }

    if (msg_len != sizeof(struct cc_msg_dma_direction)) {
        DOCA_LOG_ERR("Negotiation with DPU on file location and size failed");
        return DOCA_ERROR_INVALID_VALUE;
    }

    if (!cfg->is_file_found_locally)
        cfg->file_size = ntohq(dpu_dma_direction.file_size);

    DOCA_LOG_INFO("Negotiation with DPU on file location and size ended successfully");
    return DOCA_SUCCESS;
}

/*
 * Host side function for exporting memory map to DPU side with Comm Channel
 *
 * @mmap [in]: DOCA memory map
 * @dev [in]: DOCA device
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @export_desc [out]: Export descriptor to send
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t host_export_memory_map_to_dpu(struct doca_mmap *mmap,
                          struct doca_dev *dev,
                          struct doca_comm_channel_ep_t *ep,
                          struct doca_comm_channel_addr_t **peer_addr,
                          const void **export_desc)
{
    doca_error_t result;
    size_t export_desc_len;
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };

    /* Export memory map to allow access to this memory region from DPU */
    result = doca_mmap_export_pci(mmap, dev, export_desc, &export_desc_len);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to export DOCA mmap: %s", doca_error_get_descr(result));
        return result;
    }

    /* Send the memory map export descriptor to DPU */
    while ((result = doca_comm_channel_ep_sendto(ep,
                             *export_desc,
                             export_desc_len,
                             DOCA_CC_MSG_FLAG_NONE,
                             *peer_addr)) == DOCA_ERROR_AGAIN)
        nanosleep(&ts, &ts);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to send config files to DPU: %s", doca_error_get_descr(result));
        return result;
    }

    result = wait_for_successful_status_msg(ep, peer_addr);
    if (result != DOCA_SUCCESS)
        return result;

    return DOCA_SUCCESS;
}

/*
 * Allocate memory and populate it into the memory map
 *
 * @mmap [in]: DOCA memory map
 * @buffer_len [in]: Allocated buffer length
 * @access_flags [in]: The access permissions of the mmap
 * @buffer [out]: Allocated buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t memory_alloc_and_populate(struct doca_mmap *mmap,
                          size_t buffer_len,
                          uint32_t access_flags,
                          char **buffer)
{
    doca_error_t result;

    result = doca_mmap_set_permissions(mmap, access_flags);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to set access permissions of memory map: %s", doca_error_get_descr(result));
        return result;
    }

    // *buffer = (char *)malloc(buffer_len);
    if (*buffer == NULL) {
        DOCA_LOG_ERR("Failed to allocate memory for source buffer");
        return DOCA_ERROR_NO_MEMORY;
    }

    result = doca_mmap_set_memrange(mmap, *buffer, buffer_len);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to set memrange of memory map: %s", doca_error_get_descr(result));
        free(*buffer);
        return result;
    }

    /* Populate local buffer into memory map to allow access from DPU side after exporting */
    result = doca_mmap_start(mmap);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to populate memory map: %s", doca_error_get_descr(result));
        free(*buffer);
    }

    return result;
}

/*
 * Host side function to send buffer address and offset
 *
 * @src_buffer [in]: Buffer to send info on
 * @src_buffer_size [in]: Buffer size
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t host_send_addr_and_offset(const char *src_buffer,
                          size_t src_buffer_size,
                          struct doca_comm_channel_ep_t *ep,
                          struct doca_comm_channel_addr_t **peer_addr)
{
    doca_error_t result;
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };

    /* Send the full buffer address and length */
    uint64_t addr_to_send = htonq((uintptr_t)src_buffer);
    uint64_t length_to_send = htonq((uint64_t)src_buffer_size);

    while ((result = doca_comm_channel_ep_sendto(ep,
                             &addr_to_send,
                             sizeof(addr_to_send),
                             DOCA_CC_MSG_FLAG_NONE,
                             *peer_addr)) == DOCA_ERROR_AGAIN)
        nanosleep(&ts, &ts);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to send address to start DMA from: %s", doca_error_get_descr(result));
        return result;
    }

    result = wait_for_successful_status_msg(ep, peer_addr);
    if (result != DOCA_SUCCESS)
        return result;

    while ((result = doca_comm_channel_ep_sendto(ep,
                             &length_to_send,
                             sizeof(length_to_send),
                             DOCA_CC_MSG_FLAG_NONE,
                             *peer_addr)) == DOCA_ERROR_AGAIN)
        nanosleep(&ts, &ts);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to send config files to DPU: %s", doca_error_get_descr(result));
        return result;
    }

    result = wait_for_successful_status_msg(ep, peer_addr);
    if (result != DOCA_SUCCESS)
        return result;

    DOCA_LOG_INFO(
        "Address and offset to start DMA from sent successfully, waiting for DPU to Ack that DMA finished");

    return result;
}

/*
 * DPU side function for file size and location negotiation
 *
 * @cfg [in]: Application configuration
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @max_buf_size [in]: Maximum buffer size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t dpu_negotiate_dma_direction_and_size(struct dma_copy_cfg *cfg,
                             struct doca_comm_channel_ep_t *ep,
                             struct doca_comm_channel_addr_t **peer_addr,
                             uint64_t max_buf_size)
{
    struct cc_msg_dma_direction host_dma_direction = {0};
    struct cc_msg_dma_direction dpu_dma_direction = {0};
    struct cc_msg_dma_status status_msg = {.is_success = false};
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };
    doca_error_t result;
    size_t msg_len;

    if (cfg->is_file_found_locally) {
        DOCA_LOG_INFO("File was found locally, it will be DMA copied to the Host");
        dpu_dma_direction.file_in_host = false;
        dpu_dma_direction.file_size = htonq(cfg->file_size);
    } else {
        DOCA_LOG_INFO("File was not found locally, it will be DMA copied from the Host");
        dpu_dma_direction.file_in_host = true;
    }

    result = doca_comm_channel_ep_listen(ep, SERVER_NAME);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Comm Channel endpoint couldn't start listening: %s", doca_error_get_descr(result));
        return result;
    }

    DOCA_LOG_INFO("Waiting for Host to send negotiation message");

    /* Wait until Host negotiation message will arrive */
    msg_len = sizeof(struct cc_msg_dma_direction);
    while ((result = doca_comm_channel_ep_recvfrom(ep,
                               (void *)&host_dma_direction,
                               &msg_len,
                               DOCA_CC_MSG_FLAG_NONE,
                               peer_addr)) == DOCA_ERROR_AGAIN) {
        nanosleep(&ts, &ts);
        msg_len = sizeof(struct cc_msg_dma_direction);
    }
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Response message was not received: %s", doca_error_get_descr(result));
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return result;
    }

    if (msg_len != sizeof(struct cc_msg_dma_direction)) {
        DOCA_LOG_ERR("Response negotiation message was not received correctly");
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return DOCA_ERROR_INVALID_VALUE;
    }

    /* Make sure file is located only on one side */
    if (cfg->is_file_found_locally && host_dma_direction.file_in_host == true) {
        DOCA_LOG_ERR("Error - File was found on both Host and DPU");
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return DOCA_ERROR_INVALID_VALUE;

    } else if (!cfg->is_file_found_locally) {
        if (!host_dma_direction.file_in_host) {
            DOCA_LOG_ERR("Error - File was not found on both Host and DPU");
            send_status_msg(ep, peer_addr, STATUS_FAILURE);
            return DOCA_ERROR_INVALID_VALUE;
        }
        cfg->file_size = ntohq(host_dma_direction.file_size);
    }

    /* Verify file size against the HW limitation */
    if (cfg->file_size > max_buf_size) {
        /* Send failure message to Host */
        DOCA_LOG_ERR("DMA device maximum allowed file size in bytes is %" PRIu64
                 ", received file size is %" PRIu64 " bytes",
                 max_buf_size,
                 cfg->file_size);
        while ((result = doca_comm_channel_ep_sendto(ep,
                                 &status_msg,
                                 sizeof(struct cc_msg_dma_status),
                                 DOCA_CC_MSG_FLAG_NONE,
                                 *peer_addr)) == DOCA_ERROR_AGAIN)
            nanosleep(&ts, &ts);

        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to send failure status message to Host: %s", doca_error_get_descr(result));
            return result;
        }

        result = DOCA_ERROR_INVALID_VALUE;
    } else {
        /* Send direction message to Host to end negotiation */
        while ((result = doca_comm_channel_ep_sendto(ep,
                                 &dpu_dma_direction,
                                 sizeof(struct cc_msg_dma_direction),
                                 DOCA_CC_MSG_FLAG_NONE,
                                 *peer_addr)) == DOCA_ERROR_AGAIN)
            nanosleep(&ts, &ts);

        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to send final negotiation message to Host: %s",
                     doca_error_get_descr(result));
    }

    return result;
}

/*
 * DPU side function for receiving export descriptor on Comm Channel
 *
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @export_desc_buffer [out]: Buffer to save the export descriptor
 * @export_desc_len [out]: Export descriptor length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t dpu_receive_export_desc(struct doca_comm_channel_ep_t *ep,
                        struct doca_comm_channel_addr_t **peer_addr,
                        char *export_desc_buffer,
                        size_t *export_desc_len)
{
    size_t msg_len;
    doca_error_t result;
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };

    DOCA_LOG_INFO("Waiting for Host to send export descriptor");

    /* Receive exported descriptor from Host */
    msg_len = CC_MAX_MSG_SIZE;
    while ((result = doca_comm_channel_ep_recvfrom(ep,
                               (void *)export_desc_buffer,
                               &msg_len,
                               DOCA_CC_MSG_FLAG_NONE,
                               peer_addr)) == DOCA_ERROR_AGAIN) {
        nanosleep(&ts, &ts);
        msg_len = CC_MAX_MSG_SIZE;
    }
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to receive export descriptor from Host: %s", doca_error_get_descr(result));
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return result;
    }

    *export_desc_len = msg_len;
    DOCA_LOG_TRC("Export descriptor received successfully from Host");

    result = send_status_msg(ep, peer_addr, STATUS_SUCCESS);
    if (result != DOCA_SUCCESS)
        return result;

    return result;
}

/*
 * DPU side function for receiving remote buffer address and offset on Comm Channel
 *
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @host_addr [out]: Remote buffer address
 * @host_offset [out]: Remote buffer offset
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t dpu_receive_addr_and_offset(struct doca_comm_channel_ep_t *ep,
                        struct doca_comm_channel_addr_t **peer_addr,
                        char **host_addr,
                        size_t *host_offset)
{
    doca_error_t result;
    uint64_t received_addr, received_addr_len;
    size_t msg_len;
    struct timespec ts = {
        .tv_nsec = SLEEP_IN_NANOS,
    };

    DOCA_LOG_INFO("Waiting for Host to send address and offset");

    /* Receive remote source buffer address */
    msg_len = sizeof(received_addr);
    while ((result = doca_comm_channel_ep_recvfrom(ep,
                               (void *)&received_addr,
                               &msg_len,
                               DOCA_CC_MSG_FLAG_NONE,
                               peer_addr)) == DOCA_ERROR_AGAIN) {
        nanosleep(&ts, &ts);
        msg_len = sizeof(received_addr);
    }
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to receive remote address from Host: %s", doca_error_get_descr(result));
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return result;
    }

    received_addr = ntohq(received_addr);
    if (received_addr > SIZE_MAX) {
        DOCA_LOG_ERR("Address size exceeds pointer size in this device");
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return DOCA_ERROR_INVALID_VALUE;
    }
    *host_addr = (char *)received_addr;

    DOCA_LOG_TRC("Remote address received successfully from Host: %" PRIu64 "", received_addr);

    result = send_status_msg(ep, peer_addr, STATUS_SUCCESS);
    if (result != DOCA_SUCCESS)
        return result;

    /* Receive remote source buffer length */
    msg_len = sizeof(received_addr_len);
    while ((result = doca_comm_channel_ep_recvfrom(ep,
                               (void *)&received_addr_len,
                               &msg_len,
                               DOCA_CC_MSG_FLAG_NONE,
                               peer_addr)) == DOCA_ERROR_AGAIN) {
        nanosleep(&ts, &ts);
        msg_len = sizeof(received_addr_len);
    }
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to receive remote address offset from Host: %s", doca_error_get_descr(result));
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return result;
    }

    received_addr_len = ntohq(received_addr_len);
    if (received_addr_len > SIZE_MAX) {
        DOCA_LOG_ERR("Offset exceeds SIZE_MAX in this device");
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        return DOCA_ERROR_INVALID_VALUE;
    }
    *host_offset = (size_t)received_addr_len;

    DOCA_LOG_TRC("Address offset received successfully from Host: %" PRIu64 "", received_addr_len);

    result = send_status_msg(ep, peer_addr, STATUS_SUCCESS);

    return result;
}

/*
 * DPU side function for submitting DMA task into the progress engine, wait for its completion and save it into a file
 * if needed.
 *
 * @cfg [in]: Application configuration
 * @resources [in]: DMA copy resources
 * @bytes_to_copy [in]: Number of bytes to DMA copy
 * @buffer [in]: local DMA buffer
 * @local_doca_buf [in]: local DOCA buffer
 * @remote_doca_buf [in]: remote DOCA buffer
 * @num_remaining_tasks [in]: Number of remaining tasks
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t dpu_submit_dma_task(struct dma_copy_cfg *cfg,
                    struct dma_copy_resources *resources,
                    size_t bytes_to_copy,
                    char *buffer,
                    struct doca_buf *local_doca_buf,
                    struct doca_buf *remote_doca_buf,
                    size_t *num_remaining_tasks)
{
    struct program_core_objects *state = resources->state;
    struct doca_dma_task_memcpy *dma_task;
    struct doca_task *task;
    union doca_data task_user_data = {0};
    void *data;
    struct doca_buf *src_buf;
    struct doca_buf *dst_buf;
    struct timespec ts = {
        .tv_sec = 0,
        .tv_nsec = SLEEP_IN_NANOS,
    };
    doca_error_t result;
    doca_error_t task_result;

    /* Determine DMA copy direction */
    if (cfg->is_file_found_locally) {
        src_buf = local_doca_buf;
        dst_buf = remote_doca_buf;
    } else {
        src_buf = remote_doca_buf;
        dst_buf = local_doca_buf;
    }

    /* Set data position in src_buf */
    result = doca_buf_get_data(src_buf, &data);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get data address from DOCA buffer: %s", doca_error_get_descr(result));
        return result;
    }
    result = doca_buf_set_data(src_buf, data, bytes_to_copy);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set data for DOCA buffer: %s", doca_error_get_descr(result));
        return result;
    }

    /* Include result in user data of task to be used in the callbacks */
    task_user_data.ptr = &task_result;
    /* Allocate and construct DMA task */
    result = doca_dma_task_memcpy_alloc_init(resources->dma_ctx, src_buf, dst_buf, task_user_data, &dma_task);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to allocate DMA memcpy task: %s", doca_error_get_descr(result));
        return result;
    }

    task = doca_dma_task_memcpy_as_task(dma_task);

    /* Submit DMA task */
    result = doca_task_submit(task);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to submit DMA task: %s", doca_error_get_descr(result));
        goto free_task;
    }

    /* Wait for all tasks to be completed */
    while (*num_remaining_tasks > 0) {
        if (doca_pe_progress(state->pe) == 0)
            nanosleep(&ts, &ts);
    }

    /* Check result of task according to the result we update in the callbacks */
    if (task_result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("DMA copy failed: %s", doca_error_get_descr(task_result));
        result = task_result;
        goto free_task;
    }

    DOCA_LOG_INFO("DMA copy was done Successfully");

    /* If the buffer was copied into to DPU, save it as a file */
    if (!cfg->is_file_found_locally) {
        DOCA_LOG_INFO("Writing DMA buffer into a file on %s", cfg->file_path);
        result = save_buffer_into_a_file(cfg, buffer);
        if (result != DOCA_SUCCESS)
            return result;
    }

free_task:
    doca_task_free(task);
    return result;
}

/*
 * Check if DOCA device is DMA capable
 *
 * @devinfo [in]: Device to check
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t check_dev_dma_capable(struct doca_devinfo *devinfo)
{
    return doca_dma_cap_task_memcpy_is_supported(devinfo);
}

/*
 * Set Comm Channel properties
 *
 * @mode [in]: Running mode
 * @ep [in]: DOCA comm_channel endpoint
 * @dev [in]: DOCA device object to use
 * @dev_rep [in]: DOCA device representor object to use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t set_cc_properties(enum dma_copy_mode mode,
                      struct doca_comm_channel_ep_t *ep,
                      struct doca_dev *dev,
                      struct doca_dev_rep *dev_rep)
{
    doca_error_t result;

    result = doca_comm_channel_ep_set_device(ep, dev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set DOCA device property");
        return result;
    }

    result = doca_comm_channel_ep_set_max_msg_size(ep, CC_MAX_MSG_SIZE);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set max_msg_size property");
        return result;
    }

    result = doca_comm_channel_ep_set_send_queue_size(ep, CC_MAX_QUEUE_SIZE);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set snd_queue_size property");
        return result;
    }

    result = doca_comm_channel_ep_set_recv_queue_size(ep, CC_MAX_QUEUE_SIZE);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set rcv_queue_size property");
        return result;
    }

    if (mode == DMA_COPY_MODE_DPU) {
        result = doca_comm_channel_ep_set_device_rep(ep, dev_rep);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to set DOCA device representor property");
    }

    return result;
}

doca_error_t destroy_cc(struct doca_comm_channel_ep_t *ep,
            struct doca_comm_channel_addr_t *peer,
            struct doca_dev *dev,
            struct doca_dev_rep *dev_rep)
{
    doca_error_t result = DOCA_SUCCESS, tmp_result;

    if (peer != NULL) {
        tmp_result = doca_comm_channel_ep_disconnect(ep, peer);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to disconnect from Comm Channel peer address: %s",
                     doca_error_get_descr(tmp_result));
        }
    }

    tmp_result = doca_comm_channel_ep_destroy(ep);
    if (result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy DOCA Comm Channel endpoint: %s", doca_error_get_descr(tmp_result));
    }

    if (dev_rep != NULL) {
        tmp_result = doca_dev_rep_close(dev_rep);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to close Comm Channel DOCA device representor: %s",
                     doca_error_get_descr(tmp_result));
        }
    }

    tmp_result = doca_dev_close(dev);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to close Comm Channel DOCA device: %s", doca_error_get_descr(tmp_result));
    }

    return result;
}

doca_error_t init_cc(struct dma_copy_cfg *cfg,
             struct doca_comm_channel_ep_t **ep,
             struct doca_dev **dev,
             struct doca_dev_rep **dev_rep)
{
    doca_error_t result, tmp_result;

    result = doca_comm_channel_ep_create(ep);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create Comm Channel endpoint: %s", doca_error_get_descr(result));
        return result;
    }

    result = open_doca_device_with_pci(cfg->cc_dev_pci_addr, NULL, dev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to open Comm Channel DOCA device based on PCI address");
        goto destroy_ep;
    }

    /* Open DOCA device representor on DPU side */
    if (cfg->mode == DMA_COPY_MODE_DPU) {
        result = open_doca_device_rep_with_pci(*dev,
                               DOCA_DEVINFO_REP_FILTER_NET,
                               cfg->cc_dev_rep_pci_addr,
                               dev_rep);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to open Comm Channel DOCA device representor based on PCI address");
            goto close_device;
        }
    }

    result = set_cc_properties(cfg->mode, *ep, *dev, *dev_rep);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set Comm Channel properties");
        if (cfg->mode == DMA_COPY_MODE_DPU)
            doca_dev_rep_close(*dev_rep);
        goto close_device;
    }

    return result;

close_device:
    tmp_result = doca_dev_close(*dev);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy close DOCA device: %s", doca_error_get_descr(tmp_result));
    }
destroy_ep:
    tmp_result = doca_comm_channel_ep_destroy(*ep);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy remote DOCA Comm Channel endpoint: %s",
                 doca_error_get_descr(tmp_result));
    }

    return result;
}

doca_error_t register_dma_copy_params(void)
{
    doca_error_t result;
    struct doca_argp_param *file_path_param, *dev_pci_addr_param, *rep_pci_addr_param;

    /* Create and register string to dma copy param */
    result = doca_argp_param_create(&file_path_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(file_path_param, "f");
    doca_argp_param_set_long_name(file_path_param, "file");
    doca_argp_param_set_description(file_path_param,
                    "Full path to file to be copied/created after a successful DMA copy");
    doca_argp_param_set_callback(file_path_param, file_path_callback);
    doca_argp_param_set_type(file_path_param, DOCA_ARGP_TYPE_STRING);
    doca_argp_param_set_mandatory(file_path_param);
    result = doca_argp_register_param(file_path_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
        return result;
    }

    /* Create and register Comm Channel DOCA device PCI address */
    result = doca_argp_param_create(&dev_pci_addr_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(dev_pci_addr_param, "p");
    doca_argp_param_set_long_name(dev_pci_addr_param, "pci-addr");
    doca_argp_param_set_description(dev_pci_addr_param, "DOCA Comm Channel device PCI address");
    doca_argp_param_set_callback(dev_pci_addr_param, dev_pci_addr_callback);
    doca_argp_param_set_type(dev_pci_addr_param, DOCA_ARGP_TYPE_STRING);
    doca_argp_param_set_mandatory(dev_pci_addr_param);
    result = doca_argp_register_param(dev_pci_addr_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
        return result;
    }

    /* Create and register Comm Channel DOCA device representor PCI address */
    result = doca_argp_param_create(&rep_pci_addr_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(rep_pci_addr_param, "r");
    doca_argp_param_set_long_name(rep_pci_addr_param, "rep-pci");
    doca_argp_param_set_description(rep_pci_addr_param,
                    "DOCA Comm Channel device representor PCI address (needed only on DPU)");
    doca_argp_param_set_callback(rep_pci_addr_param, rep_pci_addr_callback);
    doca_argp_param_set_type(rep_pci_addr_param, DOCA_ARGP_TYPE_STRING);
    result = doca_argp_register_param(rep_pci_addr_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
        return result;
    }

    /* Register validation callback */
    result = doca_argp_register_validation_callback(args_validation_callback);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program validation callback: %s", doca_error_get_descr(result));
        return result;
    }

    /* Register version callback for DOCA SDK & RUNTIME */
    result = doca_argp_register_version_callback(sdk_version_callback);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register version callback: %s", doca_error_get_descr(result));
        return result;
    }

    return DOCA_SUCCESS;
}

doca_error_t open_dma_device(struct doca_dev **dev)
{
    doca_error_t result;

    result = open_doca_device_with_capabilities(check_dev_dma_capable, dev);
    if (result != DOCA_SUCCESS)
        DOCA_LOG_ERR("Failed to open DOCA DMA capable device");

    return result;
}

/*
 * DMA Memcpy task completed callback
 *
 * @dma_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void dma_memcpy_completed_callback(struct doca_dma_task_memcpy *dma_task,
                      union doca_data task_user_data,
                      union doca_data ctx_user_data)
{
    size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
    doca_error_t *result = (doca_error_t *)task_user_data.ptr;

    (void)dma_task;
    /* Decrement number of remaining tasks */
    --*num_remaining_tasks;
    /* Assign success to the result */
    *result = DOCA_SUCCESS;
}

/*
 * Memcpy task error callback
 *
 * @dma_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void dma_memcpy_error_callback(struct doca_dma_task_memcpy *dma_task,
                      union doca_data task_user_data,
                      union doca_data ctx_user_data)
{
    size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
    struct doca_task *task = doca_dma_task_memcpy_as_task(dma_task);
    doca_error_t *result = (doca_error_t *)task_user_data.ptr;

    /* Decrement number of remaining tasks */
    --*num_remaining_tasks;
    /* Get the result of the task */
    *result = doca_task_get_status(task);
}

/*
 * Destroy copy resources
 *
 * @resources [in]: DMA copy resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t destroy_dma_copy_resources(struct dma_copy_resources *resources)
{
    struct program_core_objects *state = resources->state;
    doca_error_t result = DOCA_SUCCESS, tmp_result;

    if (resources->dma_ctx != NULL) {
        tmp_result = doca_dma_destroy(resources->dma_ctx);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to destroy DOCA DMA context: %s", doca_error_get_descr(tmp_result));
        }
    }

    tmp_result = destroy_core_objects(state);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy DOCA core objects: %s", doca_error_get_descr(tmp_result));
    }

    free(resources->state);

    return result;
}

/*
 * Allocate DMA copy resources
 *
 * @resources [out]: DOCA DMA copy resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t allocate_dma_copy_resources(struct dma_copy_resources *resources)
{
    struct program_core_objects *state = NULL;
    doca_error_t result, tmp_result;
    /* Two buffers for source and destination */
    uint32_t max_bufs = 2;

    resources->state = malloc(sizeof(*(resources->state)));
    if (resources->state == NULL) {
        result = DOCA_ERROR_NO_MEMORY;
        DOCA_LOG_ERR("Failed to allocate DOCA program core objects: %s", doca_error_get_descr(result));
        return result;
    }
    state = resources->state;

    /* Open DOCA dma device */
    result = open_dma_device(&state->dev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to open DMA device: %s", doca_error_get_descr(result));
        goto free_state;
    }

    result = create_core_objects(state, max_bufs);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create core objects: %s", doca_error_get_descr(result));
        goto destroy_core_objects;
    }

    result = doca_dma_create(state->dev, &resources->dma_ctx);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to create DOCA DMA context: %s", doca_error_get_descr(result));
        goto destroy_core_objects;
    }

    state->ctx = doca_dma_as_ctx(resources->dma_ctx);

    result = doca_pe_connect_ctx(state->pe, state->ctx);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to set DOCA progress engine to DOCA DMA: %s", doca_error_get_descr(result));
        goto destroy_dma;
    }

    result = doca_dma_task_memcpy_set_conf(resources->dma_ctx,
                           dma_memcpy_completed_callback,
                           dma_memcpy_error_callback,
                           NUM_DMA_TASKS);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to set configurations for DMA memcpy task: %s", doca_error_get_descr(result));
        goto destroy_dma;
    }

    return result;

destroy_dma:
    tmp_result = doca_dma_destroy(resources->dma_ctx);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy DOCA DMA context: %s", doca_error_get_descr(tmp_result));
    }
destroy_core_objects:
    tmp_result = destroy_core_objects(state);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy DOCA core objects: %s", doca_error_get_descr(tmp_result));
    }
free_state:
    free(resources->state);

    return result;
}

doca_error_t host_start_dma_copy(struct dma_copy_cfg *dma_cfg,
                 struct doca_comm_channel_ep_t *ep,
                 struct doca_comm_channel_addr_t **peer_addr,
                 struct spright_cfg_s *spright_cfg)
{
    struct doca_mmap *mmap = NULL;
    struct doca_dev *dev = NULL;
    // char *buffer = NULL;
    const void *export_desc = NULL;
    doca_error_t result, tmp_result;

    if (spright_cfg == NULL)
        return DOCA_ERROR_UNKNOWN;
    printf("0x%" PRIXPTR ", len: %lu \n", (uintptr_t)spright_cfg, sizeof(struct spright_cfg_s));

    /* Negotiate DMA copy direction with DPU */
    result = host_negotiate_dma_direction_and_size(dma_cfg, ep, peer_addr);
    if (result != DOCA_SUCCESS)
        return result;

    /* Allocate memory to be used for read operation in case file is found locally, otherwise grant write access */
    uint32_t dpu_access = dma_cfg->is_file_found_locally ? DOCA_ACCESS_FLAG_PCI_READ_ONLY :
                                   DOCA_ACCESS_FLAG_PCI_READ_WRITE;

    /* Open DOCA dma device */
    result = open_dma_device(&dev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to open DOCA DMA device: %s", doca_error_get_descr(result));
        return result;
    }

    result = doca_mmap_create(&mmap);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to create mmap: %s", doca_error_get_descr(result));
        goto close_device;
    }

    result = doca_mmap_add_dev(mmap, dev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to add device to mmap: %s", doca_error_get_descr(result));
        goto destroy_mmap;
    }

    // result = memory_alloc_and_populate(mmap, dma_cfg->file_size, dpu_access, &buffer);
    result = memory_alloc_and_populate(mmap, sizeof(struct spright_cfg_s), dpu_access, (char**) &spright_cfg);
    if (result != DOCA_SUCCESS)
        goto destroy_mmap;

    // Test local mmap

    /* Export memory map and send it to DPU */
    result = host_export_memory_map_to_dpu(mmap, dev, ep, peer_addr, &export_desc);
    if (result != DOCA_SUCCESS)
        goto free_buffer;

    /* Fill the buffer before DPU starts DMA operation */
    // if (dma_cfg->is_file_found_locally) {
    //     result = fill_buffer_with_file_content(dma_cfg, buffer);
    //     if (result != DOCA_SUCCESS)
    //         goto free_buffer;
    // }

    /* Send source buffer address and offset (entire buffer) to enable DMA and wait until DPU is done */
    // result = host_send_addr_and_offset(buffer, dma_cfg->file_size, ep, peer_addr);
    result = host_send_addr_and_offset((char*) spright_cfg, sizeof(struct spright_cfg_s), ep, peer_addr);
    if (result != DOCA_SUCCESS)
        goto free_buffer;

    /* Wait to DPU status message to indicate DMA was ended */
    result = wait_for_successful_status_msg(ep, peer_addr);
    if (result != DOCA_SUCCESS)
        goto free_buffer;

    DOCA_LOG_INFO("Final status message was successfully received");
    sleep(30);

    // if (!dma_cfg->is_file_found_locally) {
    //     /*  File was copied successfully into the buffer, save it into file */
    //     DOCA_LOG_INFO("Writing DMA buffer into a file on %s", dma_cfg->file_path);
    //     result = save_buffer_into_a_file(dma_cfg, buffer);
    // }

free_buffer:
    // free(buffer);
destroy_mmap:
    tmp_result = doca_mmap_destroy(mmap);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to destroy DOCA mmap: %s", doca_error_get_descr(tmp_result));
        DOCA_ERROR_PROPAGATE(result, tmp_result);
    }
close_device:
    tmp_result = doca_dev_close(dev);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(tmp_result));
        DOCA_ERROR_PROPAGATE(result, tmp_result);
    }
    return result;
}

doca_error_t dpu_start_dma_copy(struct dma_copy_cfg *dma_cfg,
                struct doca_comm_channel_ep_t *ep,
                struct doca_comm_channel_addr_t **peer_addr)
{
    struct dma_copy_resources resources = {0};
    struct program_core_objects *state = NULL;
    /* Allocate memory to be used for read operation in case file is found locally, otherwise grant write access */
    uint32_t access_flags = dma_cfg->is_file_found_locally ? DOCA_ACCESS_FLAG_LOCAL_READ_ONLY :
                                 DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
    uint64_t max_buf_size;
    char *buffer;
    char *host_dma_addr = NULL;
    char export_desc_buf[CC_MAX_MSG_SIZE];
    struct doca_buf *remote_doca_buf = NULL;
    struct doca_buf *local_doca_buf = NULL;
    struct doca_mmap *remote_mmap = NULL;
    size_t host_dma_offset, export_desc_len;
    union doca_data ctx_user_data = {0};
    /* Number of tasks submitted to progress engine */
    size_t num_remaining_tasks = 1;
    doca_error_t result, tmp_result;

    /* Allocate DMA copy resources */
    result = allocate_dma_copy_resources(&resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to allocate DMA copy resources: %s", doca_error_get_descr(result));
        return result;
    }
    state = resources.state;

    result = get_dma_max_buf_size(&resources, &max_buf_size);
    if (result != DOCA_SUCCESS)
        goto destroy_dma_resources;

    /* Include tasks counter in user data of context to be decremented in callbacks */
    ctx_user_data.ptr = &num_remaining_tasks;
    doca_ctx_set_user_data(state->ctx, ctx_user_data);

    result = doca_ctx_start(state->ctx);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to start DMA context: %s", doca_error_get_descr(result));
        goto destroy_dma_resources;
    }

    /* Negotiate DMA copy direction with Host */
    result = dpu_negotiate_dma_direction_and_size(dma_cfg, ep, peer_addr, max_buf_size);
    if (result != DOCA_SUCCESS)
        goto stop_dma;

    // struct spright_cfg_s *buffer;
    buffer = malloc(sizeof(struct spright_cfg_s));

    // result = memory_alloc_and_populate(state->src_mmap, dma_cfg->file_size, access_flags, &buffer);
    result = memory_alloc_and_populate(state->src_mmap, sizeof(struct spright_cfg_s), access_flags, &buffer);
    if (result != DOCA_SUCCESS)
        goto stop_dma;

    /* Receive export descriptor from Host */
    result = dpu_receive_export_desc(ep, peer_addr, export_desc_buf, &export_desc_len);
    if (result != DOCA_SUCCESS)
        goto free_buffer;

    /* Create a local DOCA mmap from export descriptor */
    result = doca_mmap_create_from_export(NULL,
                          (const void *)export_desc_buf,
                          export_desc_len,
                          state->dev,
                          &remote_mmap);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create memory map from export: %s", doca_error_get_descr(result));
        goto free_buffer;
    }

    /* Receive remote address and offset from Host */
    result = dpu_receive_addr_and_offset(ep, peer_addr, &host_dma_addr, &host_dma_offset);
    if (result != DOCA_SUCCESS)
        goto destroy_remote_mmap;

    printf("host_dma_addr: 0x%" PRIXPTR ", host_dma_offset: %lu\n", (uintptr_t)host_dma_addr, host_dma_offset);

    /* Construct DOCA buffer for remote (Host) address range */
    result = doca_buf_inventory_buf_get_by_addr(state->buf_inv,
                            remote_mmap,
                            host_dma_addr,
                            host_dma_offset,
                            &remote_doca_buf);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to acquire DOCA remote buffer: %s", doca_error_get_descr(result));
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        goto destroy_remote_mmap;
    }

    struct spright_cfg_s *scfg;
    size_t scfg_len;
    doca_buf_get_data(remote_doca_buf, (void **)&scfg);
    doca_buf_get_data_len(remote_doca_buf, &scfg_len);
    printf("scfg: 0x%" PRIXPTR ", scfg_len: %lu\n", (uintptr_t)scfg, scfg_len);

    doca_buf_set_data(remote_doca_buf, scfg, host_dma_offset);

    spright_cfg_print((struct spright_cfg_s *)scfg);

    /* Construct DOCA buffer for local (DPU) address range */
    result = doca_buf_inventory_buf_get_by_addr(state->buf_inv,
                            state->src_mmap,
                            buffer,
                            host_dma_offset,
                            &local_doca_buf);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to acquire DOCA local buffer: %s", doca_error_get_descr(result));
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        goto destroy_remote_buf;
    }

    /* Fill buffer in file content if relevant */
    // if (dma_cfg->is_file_found_locally) {
    //     result = fill_buffer_with_file_content(dma_cfg, buffer);
    //     if (result != DOCA_SUCCESS) {
    //         send_status_msg(ep, peer_addr, STATUS_FAILURE);
    //         goto destroy_local_buf;
    //     }
    // }

    /* Submit DMA task into the progress engine and wait until task completion */
    result = dpu_submit_dma_task(dma_cfg,
                     &resources,
                     host_dma_offset,
                     buffer,
                     local_doca_buf,
                     remote_doca_buf,
                     &num_remaining_tasks);
    if (result != DOCA_SUCCESS) {
        send_status_msg(ep, peer_addr, STATUS_FAILURE);
        goto destroy_local_buf;
    }

    send_status_msg(ep, peer_addr, STATUS_SUCCESS);

    spright_cfg_print((struct spright_cfg_s *)buffer);

destroy_local_buf:
    tmp_result = doca_buf_dec_refcount(local_doca_buf, NULL);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy local DOCA buffer: %s", doca_error_get_descr(tmp_result));
    }
destroy_remote_buf:
    tmp_result = doca_buf_dec_refcount(remote_doca_buf, NULL);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy remote DOCA buffer: %s", doca_error_get_descr(tmp_result));
    }
destroy_remote_mmap:
    tmp_result = doca_mmap_destroy(remote_mmap);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy remote DOCA mmap: %s", doca_error_get_descr(tmp_result));
    }
free_buffer:
    free(buffer);
stop_dma:
    tmp_result = request_stop_ctx(state->pe, state->ctx);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Unable to stop context: %s", doca_error_get_descr(tmp_result));
    }
    state->ctx = NULL;
destroy_dma_resources:
    tmp_result = destroy_dma_copy_resources(&resources);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to destroy DMA copy resources: %s", doca_error_get_descr(tmp_result));
    }
    return result;
}


doca_error_t open_doca_device_with_pci(const char *pci_addr, tasks_check func, struct doca_dev **retval)
{
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    uint8_t is_addr_equal = 0;
    int res;
    size_t i;

    /* Set default return value */
    *retval = NULL;

    res = doca_devinfo_create_list(&dev_list, &nb_devs);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to load doca devices list: %s", doca_error_get_descr(res));
        return res;
    }

    /* Search */
    for (i = 0; i < nb_devs; i++) {
        res = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
        if (res == DOCA_SUCCESS && is_addr_equal) {
            /* If any special capabilities are needed */
            if (func != NULL && func(dev_list[i]) != DOCA_SUCCESS)
                continue;

            /* if device can be opened */
            res = doca_dev_open(dev_list[i], retval);
            if (res == DOCA_SUCCESS) {
                doca_devinfo_destroy_list(dev_list);
                return res;
            }
        }
    }

    DOCA_LOG_WARN("Matching device not found");
    res = DOCA_ERROR_NOT_FOUND;

    doca_devinfo_destroy_list(dev_list);
    return res;
}

doca_error_t open_doca_device_with_capabilities(tasks_check func, struct doca_dev **retval)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs;
	doca_error_t result;
	size_t i;

	/* Set default return value */
	*retval = NULL;

	result = doca_devinfo_create_list(&dev_list, &nb_devs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to load doca devices list: %s", doca_error_get_descr(result));
		return result;
	}

	/* Search */
	for (i = 0; i < nb_devs; i++) {
		/* If any special capabilities are needed */
		if (func(dev_list[i]) != DOCA_SUCCESS)
			continue;

		/* If device can be opened */
		if (doca_dev_open(dev_list[i], retval) == DOCA_SUCCESS) {
			doca_devinfo_destroy_list(dev_list);
			return DOCA_SUCCESS;
		}
	}

	DOCA_LOG_WARN("Matching device not found");
	doca_devinfo_destroy_list(dev_list);
	return DOCA_ERROR_NOT_FOUND;
}

doca_error_t open_doca_device_rep_with_pci(struct doca_dev *local,
                       enum doca_devinfo_rep_filter filter,
                       const char *pci_addr,
                       struct doca_dev_rep **retval)
{
    uint32_t nb_rdevs = 0;
    struct doca_devinfo_rep **rep_dev_list = NULL;
    uint8_t is_addr_equal = 0;
    doca_error_t result;
    size_t i;

    *retval = NULL;

    /* Search */
    result = doca_devinfo_rep_create_list(local, filter, &rep_dev_list, &nb_rdevs);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR(
            "Failed to create devinfo representors list. Representor devices are available only on DPU, do not run on Host");
        return DOCA_ERROR_INVALID_VALUE;
    }

    for (i = 0; i < nb_rdevs; i++) {
        result = doca_devinfo_rep_is_equal_pci_addr(rep_dev_list[i], pci_addr, &is_addr_equal);
        if (result == DOCA_SUCCESS && is_addr_equal &&
            doca_dev_rep_open(rep_dev_list[i], retval) == DOCA_SUCCESS) {
            doca_devinfo_rep_destroy_list(rep_dev_list);
            return DOCA_SUCCESS;
        }
    }

    DOCA_LOG_WARN("Matching device not found");
    doca_devinfo_rep_destroy_list(rep_dev_list);
    return DOCA_ERROR_NOT_FOUND;
}

doca_error_t create_core_objects(struct program_core_objects *state, uint32_t max_bufs)
{
    doca_error_t res;

    res = doca_mmap_create(&state->src_mmap);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to create source mmap: %s", doca_error_get_descr(res));
        return res;
    }
    res = doca_mmap_add_dev(state->src_mmap, state->dev);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to add device to source mmap: %s", doca_error_get_descr(res));
        goto destroy_src_mmap;
    }

    res = doca_mmap_create(&state->dst_mmap);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to create destination mmap: %s", doca_error_get_descr(res));
        goto destroy_src_mmap;
    }
    res = doca_mmap_add_dev(state->dst_mmap, state->dev);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to add device to destination mmap: %s", doca_error_get_descr(res));
        goto destroy_dst_mmap;
    }

    if (max_bufs != 0) {
        res = doca_buf_inventory_create(max_bufs, &state->buf_inv);
        if (res != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Unable to create buffer inventory: %s", doca_error_get_descr(res));
            goto destroy_dst_mmap;
        }

        res = doca_buf_inventory_start(state->buf_inv);
        if (res != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Unable to start buffer inventory: %s", doca_error_get_descr(res));
            goto destroy_buf_inv;
        }
    }

    res = doca_pe_create(&state->pe);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Unable to create progress engine: %s", doca_error_get_descr(res));
        goto destroy_buf_inv;
    }

    return DOCA_SUCCESS;

destroy_buf_inv:
    if (state->buf_inv != NULL) {
        doca_buf_inventory_destroy(state->buf_inv);
        state->buf_inv = NULL;
    }

destroy_dst_mmap:
    doca_mmap_destroy(state->dst_mmap);
    state->dst_mmap = NULL;

destroy_src_mmap:
    doca_mmap_destroy(state->src_mmap);
    state->src_mmap = NULL;

    return res;
}

doca_error_t request_stop_ctx(struct doca_pe *pe, struct doca_ctx *ctx)
{
    doca_error_t tmp_result, result = DOCA_SUCCESS;

    tmp_result = doca_ctx_stop(ctx);
    if (tmp_result == DOCA_ERROR_IN_PROGRESS) {
        enum doca_ctx_states ctx_state;

        do {
            (void)doca_pe_progress(pe);
            tmp_result = doca_ctx_get_state(ctx, &ctx_state);
            if (tmp_result != DOCA_SUCCESS) {
                DOCA_ERROR_PROPAGATE(result, tmp_result);
                DOCA_LOG_ERR("Failed to get state from ctx: %s", doca_error_get_descr(tmp_result));
                break;
            }
        } while (ctx_state != DOCA_CTX_STATE_IDLE);
    } else if (tmp_result != DOCA_SUCCESS) {
        DOCA_ERROR_PROPAGATE(result, tmp_result);
        DOCA_LOG_ERR("Failed to stop ctx: %s", doca_error_get_descr(tmp_result));
    }

    return result;
}

doca_error_t destroy_core_objects(struct program_core_objects *state)
{
    doca_error_t tmp_result, result = DOCA_SUCCESS;

    if (state->pe != NULL) {
        tmp_result = doca_pe_destroy(state->pe);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to destroy pe: %s", doca_error_get_descr(tmp_result));
        }
        state->pe = NULL;
    }

    if (state->buf_inv != NULL) {
        tmp_result = doca_buf_inventory_destroy(state->buf_inv);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to destroy buf inventory: %s", doca_error_get_descr(tmp_result));
        }
        state->buf_inv = NULL;
    }

    if (state->dst_mmap != NULL) {
        tmp_result = doca_mmap_destroy(state->dst_mmap);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to destroy destination mmap: %s", doca_error_get_descr(tmp_result));
        }
        state->dst_mmap = NULL;
    }

    if (state->src_mmap != NULL) {
        tmp_result = doca_mmap_destroy(state->src_mmap);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to destroy source mmap: %s", doca_error_get_descr(tmp_result));
        }
        state->src_mmap = NULL;
    }

    if (state->dev != NULL) {
        tmp_result = doca_dev_close(state->dev);
        if (tmp_result != DOCA_SUCCESS) {
            DOCA_ERROR_PROPAGATE(result, tmp_result);
            DOCA_LOG_ERR("Failed to close device: %s", doca_error_get_descr(tmp_result));
        }
        state->dev = NULL;
    }

    return result;
}