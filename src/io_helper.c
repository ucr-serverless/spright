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

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <rte_branch_prediction.h>
#include <rte_errno.h>
#include <rte_mempool.h>

#include "io.h"

#define MAX_RETRIES 5
#define RETRY_DELAY_US 5000 // 5 milliseconds

tenant_pipe tenant_pipes[MAX_TENANTS];

/*
 * Calculate the Greatest Common Divisor
 */
static int gcd(int a, int b)
{
    while (b != 0)
    {
        int temp = b;
        b = a % b;
        a = temp;
    }

    return a;
}

/*
 * Calculate the GCD of a list of numbers
 */
int get_gcd_weight(void)
{
    if (cfg->n_tenants == 0)
        return 0;

    int result = cfg->tenants[0].weight;

    for (int i = 1; i < cfg->n_tenants; i++)
    {
        result = gcd(result, cfg->tenants[i].weight);
    }

    log_info("GCD weight: %d", result);

    return result;
}

int get_max_weight(void)
{
    int max_val = cfg->tenants[0].weight;

    for (int i = 1; i < cfg->n_tenants; i++)
    {
        if (cfg->tenants[i].weight > max_val)
        {
            max_val = cfg->tenants[i].weight;
        }
    }

    return max_val;
}

/*
 * Set file descriptor as non-blocking
 */
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        log_error("fcntl(F_GETFL) error: %s", strerror(errno));
        return -1;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1)
    {
        log_error("fcntl(F_SETFL) error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int write_pipe(struct http_transaction *txn)
{
    uint32_t tenant_id = txn->tenant_id;

    ssize_t bytes_written = write(tenant_pipes[tenant_id].fd[1], &txn, sizeof(struct http_transaction *));
    if (unlikely(bytes_written == -1))
    {
        log_error("write() error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

struct http_transaction *read_pipe(tenant_pipe *tp)
{
    struct http_transaction *txn = NULL;

    int bytes_read = read(tp->fd[0], &txn, sizeof(struct http_transaction *));
    if (bytes_read > 0)
    {
        return txn;
    }
    else if (bytes_read == -1 && errno != EAGAIN)
    {
        log_error("Error while reading pipe: %s", strerror(errno));
    }
    else if (bytes_read == 0)
    {
        log_error("Unexpected end of pipe: %s", strerror(errno));
    }

    return NULL;
}

/*
 * Initialize tenant pipes and weights
 */
int init_tenant_pipes(void)
{
    int num_tenants = cfg->n_tenants;

    log_info("Initializing %d tenant pipes and weights ...", num_tenants);

    for (int i = 0; i < num_tenants; i++)
    {
        if (pipe(tenant_pipes[i].fd) == -1)
        {
            log_error("pipe() error: %s", strerror(errno));
            return -1;
        }
        tenant_pipes[i].weight = cfg->tenants[i].weight;
        tenant_pipes[i].tenant_id = (uint32_t)i;
    }

    return 0;
}

int add_regular_pipe_to_epoll(int epoll_fd, struct epoll_event *ev, int pipe_fd)
{
    set_nonblocking(pipe_fd);
    ev->events = EPOLLIN;
    ev->data.fd = pipe_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pipe_fd, ev) == -1)
    {
        log_error("epoll_ctl(EPOLL_CTL_ADD): %s", strerror(errno));
        return -1;
    }

    return 0;
}

int add_weighted_pipes_to_epoll(int epoll_fd, struct epoll_event *ev)
{
    for (int i = 0; i < cfg->n_tenants; i++)
    {
        set_nonblocking(tenant_pipes[i].fd[0]);
        ev->events = EPOLLIN;
        ev->data.ptr = &tenant_pipes[i];

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tenant_pipes[i].fd[0], ev) == -1)
        {
            log_error("epoll_ctl(EPOLL_CTL_ADD): %s", strerror(errno));
            return -1;
        }
    }

    return 0;
}

// Helper function to read exactly count bytes from fd into buf
ssize_t read_full(int fd, void *buf, size_t count)
{
    size_t bytes_read = 0;
    ssize_t result;

    while (bytes_read < count)
    {
        result = read(fd, (char *)buf + bytes_read, count - bytes_read);

        if (result < 0)
        {
            // Error occurred
            if (errno == EINTR)
            {
                // Interrupted by signal, continue reading
                continue;
            }
            log_error("read() error: %s", strerror(errno));
            return -1;
        }
        else if (result == 0)
        {
            // EOF reached
            break;
        }

        bytes_read += result;
    }

    return bytes_read;
}

/*
 * This approach will attempt to connect to the server multiple times,
 * giving it some time to become ready. If the connection is not successful
 * within the specified number of retries, the function will return an error.
 */
int retry_connect(int sockfd, struct sockaddr *addr)
{
    int attempts = 0;
    int ret;

    do
    {
        ret = connect(sockfd, addr, sizeof(struct sockaddr_in));
        if (ret == 0)
        {
            break;
        }
        else
        {
            attempts++;
            log_warn("connect() error: %s. Retrying %d times ...", strerror(errno), attempts);
            usleep(RETRY_DELAY_US);
        }
    } while (ret == -1 && attempts < MAX_RETRIES);

    return ret;
}

// Retry mechanism for send()
ssize_t retry_send(int sockfd, const void *buf, size_t len, int flags)
{
    int attempts = 0;
    ssize_t bytes_sent;

    do 
    {
        bytes_sent = send(sockfd, buf, len, flags);
        
        // Check for successful send
        if (bytes_sent > 0)
        {
            return bytes_sent;
        }
        
        // Check for critical errors that shouldn't be retried
        if (bytes_sent == -1 && 
            (errno == EBADF || errno == ENOTSOCK || errno == EFAULT))
        {
            return bytes_sent;
        }
        
        // Increment attempts and log warning
        attempts++;
        log_warn("send() error: %s. Retrying %d/%d times ...", 
                 strerror(errno), attempts, MAX_RETRIES);
        
        // Small delay between retries
        usleep(RETRY_DELAY_US);
    } 
    while (bytes_sent == -1 && attempts < MAX_RETRIES);

    return bytes_sent;
}

// Retry mechanism for recv()
ssize_t retry_recv(int sockfd, void *buf, size_t len, int flags)
{
    int attempts = 0;
    ssize_t bytes_received;

    do 
    {
        bytes_received = recv(sockfd, buf, len, flags);
        
        // Check for successful receive
        if (bytes_received > 0)
        {
            return bytes_received;
        }
        
        // Check for connection closed
        if (bytes_received == 0)
        {
            return bytes_received;
        }
        
        // Check for critical errors that shouldn't be retried
        if (bytes_received == -1 && 
            (errno == EBADF || errno == ENOTSOCK || errno == EFAULT))
        {
            return bytes_received;
        }
        
        // Increment attempts and log warning
        attempts++;
        log_warn("recv() error: %s. Retrying %d/%d times ...", 
                 strerror(errno), attempts, MAX_RETRIES);
        
        // Small delay between retries
        usleep(RETRY_DELAY_US);
    } 
    while (bytes_received == -1 && attempts < MAX_RETRIES);

    return bytes_received;
}