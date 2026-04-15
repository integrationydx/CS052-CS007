#define _GNU_SOURCE
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <fcntl.h>

#define STACK_SIZE  (1024 * 1024)
#define MAX_CONTAINERS 10
#define SOCKET_PATH "/tmp/container_socket"
#define BUFFER_SIZE 20

// =========================================
// Container Struct
// =========================================
typedef struct {
    char id[32];
    pid_t pid;
    char state[16];
    char rootfs[128];
    time_t start_time;
    int log_fd;
    pthread_t producer_tid;
} container_t;

container_t containers[MAX_CONTAINERS];
int container_count = 0;
int shutdown_flag = 0;  // Signal for consumer to stop

// =========================================
// Args for container
// =========================================
typedef struct {
    char rootfs[128];
    int pipefd[2];
} container_args;

// =========================================
// Logging Buffer
// =========================================
typedef struct {
    char data[1024];
    char container_id[32];
} log_entry;

log_entry buffer[BUFFER_SIZE];
int in = 0, out = 0, count = 0;
pthread_mutex_t log_lock  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  not_full  = PTHREAD_COND_INITIALIZER;
pthread_cond_t  not_empty = PTHREAD_COND_INITIALIZER;

char stack[STACK_SIZE];

// =========================================
// Container Code
// =========================================
int container_main(void *arg) {
    container_args *args = (container_args *)arg;

    dup2(args->pipefd[1], STDOUT_FILENO);
    dup2(args->pipefd[1], STDERR_FILENO);
    close(args->pipefd[0]);
    close(args->pipefd[1]);

    sethostname("container", 9);
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    if (chroot(args->rootfs) != 0) {
        perror("chroot failed");
        exit(1);
    }
    chdir("/");
    mount("proc", "/proc", "proc", 0, NULL);
    execl("/bin/sh", "/bin/sh", NULL);
    perror("exec failed");
    return -1;
}

// =========================================
// Producer Thread
// =========================================
void *producer(void *arg) {
    container_t *c = (container_t *)arg;
    char buf[512];
    int n;

    // Read until the pipe is closed (container exits)
    while ((n = read(c->log_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        pthread_mutex_lock(&log_lock);

        // Wait if buffer is full
        while (count == BUFFER_SIZE && !shutdown_flag)
            pthread_cond_wait(&not_full, &log_lock);

        if (shutdown_flag) {
            pthread_mutex_unlock(&log_lock);
            break;
        }

        strcpy(buffer[in].data, buf);
        strcpy(buffer[in].container_id, c->id);
        in = (in + 1) % BUFFER_SIZE;
        count++;
        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&log_lock);
    }
    close(c->log_fd);
    return NULL;
}

// =========================================
// Consumer Thread
// =========================================
void *consumer(void *arg) {
    while (1) {
        pthread_mutex_lock(&log_lock);

        // Wait if buffer is empty, unless we are shutting down
        while (count == 0 && !shutdown_flag)
            pthread_cond_wait(&not_empty, &log_lock);

        // If shutdown triggered and buffer is drained, exit thread
        if (shutdown_flag && count == 0) {
            pthread_mutex_unlock(&log_lock);
            break;
        }

        log_entry entry = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
        count--;
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&log_lock);

        // Persistent file logging
        char path[128];
        snprintf(path, sizeof(path), "logs/%s.log", entry.container_id);
        FILE *f = fopen(path, "a");
        if (f) {
            fprintf(f, "%s", entry.data);
            fflush(f);  // Ensure data hits disk immediately for CLI 'logs' command
            fclose(f);
        }
    }
    return NULL;
}