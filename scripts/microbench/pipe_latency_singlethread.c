// Qi: This is a simple benchmark that measures the latency 
// introduced by using a Linux pipe for data transmission
// $ gcc -o pipe_latency_singlethread pipe_latency_singlethread.c
// $ ./pipe_latency_singlethread

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

#define MIN_DATA_SIZE 8   // Minimum data size
#define MAX_DATA_SIZE 8192  // Maximum data size
#define NUM_ITERATIONS 20  // Number of iterations for each data size

void error_exit(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

long measure_latency(int data_size) {
    int pipe_fds[2];
    char *data = (char *)malloc(data_size);
    struct timeval start_time, end_time;
    long latency;

    if (data == NULL) {
        error_exit("malloc failed");
    }

    memset(data, 'A', data_size);

    if (pipe(pipe_fds) == -1) {
        free(data);
        error_exit("pipe failed");
    }

    if (gettimeofday(&start_time, NULL) == -1) {
        free(data);
        error_exit("gettimeofday failed");
    }

    if (write(pipe_fds[1], data, data_size) == -1) {
        free(data);
        error_exit("write failed");
    }

    if (read(pipe_fds[0], data, data_size) == -1) {
        free(data);
        error_exit("read failed");
    }

    if (gettimeofday(&end_time, NULL) == -1) {
        free(data);
        error_exit("gettimeofday failed");
    }

    latency = (end_time.tv_sec - start_time.tv_sec) * 1000000L +
              (end_time.tv_usec - start_time.tv_usec);

    free(data);
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    return latency;
}

int main() {
    int data_size;
    long total_latency, average_latency;

    printf("Data Size (bytes)\tAverage Latency (microseconds)\n");

    for (data_size = MIN_DATA_SIZE; data_size <= MAX_DATA_SIZE; data_size *= 2) {
        total_latency = 0;

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            total_latency += measure_latency(data_size);
        }

        average_latency = total_latency / NUM_ITERATIONS;

        printf("%d\t\t\t%ld\n", data_size, average_latency);
    }

    return 0;
}
