// Qi: This is a simple benchmark that measures the latency 
// introduced by using a Linux pipe for data transmission
// $ gcc -o pipe_latency_multithreads pipe_latency_multithreads.c -lpthread
// $ ./pipe_latency_multithreads

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>

#define MIN_DATA_SIZE 8   // Minimum data size
#define MAX_DATA_SIZE 8192  // Maximum data size
#define NUM_ITERATIONS 20  // Number of iterations for each data size

typedef struct {
    int data_size;
    int pipe_fds[2];
} thread_data_t;

void error_exit(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void* writer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    char* buffer = (char*)malloc(data->data_size);
    if (buffer == NULL) {
        error_exit("malloc failed");
    }
    memset(buffer, 'A', data->data_size);

    // Write data to the pipe
    if (write(data->pipe_fds[1], buffer, data->data_size) == -1) {
        free(buffer);
        error_exit("write failed");
    }

    free(buffer);
    return NULL;
}

void* reader_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    char* buffer = (char*)malloc(data->data_size);
    if (buffer == NULL) {
        error_exit("malloc failed");
    }

    // Read data from the pipe
    if (read(data->pipe_fds[0], buffer, data->data_size) == -1) {
        free(buffer);
        error_exit("read failed");
    }

    free(buffer);
    return NULL;
}

long measure_latency(int data_size) {
    pthread_t writer, reader;
    thread_data_t thread_data;
    struct timeval start_time, end_time;
    long latency;

    // Initialize pipe and thread data
    thread_data.data_size = data_size;
    if (pipe(thread_data.pipe_fds) == -1) {
        error_exit("pipe failed");
    }

    // Get the start time
    if (gettimeofday(&start_time, NULL) == -1) {
        error_exit("gettimeofday failed");
    }

    // Create writer and reader threads
    if (pthread_create(&writer, NULL, writer_thread, &thread_data) != 0) {
        error_exit("pthread_create writer failed");
    }
    if (pthread_create(&reader, NULL, reader_thread, &thread_data) != 0) {
        error_exit("pthread_create reader failed");
    }

    // Wait for both threads to complete
    pthread_join(writer, NULL);
    pthread_join(reader, NULL);

    // Get the end time
    if (gettimeofday(&end_time, NULL) == -1) {
        error_exit("gettimeofday failed");
    }

    // Calculate the latency in microseconds
    latency = (end_time.tv_sec - start_time.tv_sec) * 1000000L +
              (end_time.tv_usec - start_time.tv_usec);

    // Close the pipe
    close(thread_data.pipe_fds[0]);
    close(thread_data.pipe_fds[1]);

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
