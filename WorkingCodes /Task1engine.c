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

#define STACK_SIZE (1024 * 1024)
#define MAX_CONTAINERS 100

// =========================================
// Container Metadata
// =========================================
typedef struct {
    char id[32];
    pid_t pid;
    time_t start_time;
    char state[16];
    int exit_status;
    char rootfs[128];
} container_t;

container_t containers[MAX_CONTAINERS];
int container_count = 0;

// =========================================
// Stack for clone()
// =========================================
char stack[STACK_SIZE];

// =========================================
// Container Code
// =========================================
int container_main(void *arg) {
    char *rootfs = (char *)arg;
    printf("[Container] Starting...\n");

    // UTS namespace -> hostname
    sethostname("container", 9);

    // Mount namespace isolation
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    // Change root
    if (chroot(rootfs) != 0) {
        perror("chroot failed");
        return -1;
    }
    chdir("/");

    // Mount /proc
    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount /proc failed");
    }

    printf("[Container] Running shell...\n");

    // Execute shell
    execl("/bin/sh", "/bin/sh", NULL);
    perror("exec failed");
    return -1;
}

// =========================================
// Create Container
// =========================================
int create_container(char *id, char *rootfs) {
    char *stack_top = stack + STACK_SIZE;

    pid_t pid = clone(container_main, stack_top,
                      CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD,
                      rootfs);
    if (pid < 0) {
        perror("clone failed");
        return -1;
    }

    // Store metadata
    strcpy(containers[container_count].id, id);
    containers[container_count].pid = pid;
    containers[container_count].start_time = time(NULL);
    strcpy(containers[container_count].state, "running");
    strcpy(containers[container_count].rootfs, rootfs);
    container_count++;

    printf("[Supervisor] Started container %s (PID %d)\n", id, pid);
    return 0;
}

// =========================================
// Reap Zombies
// =========================================
void reap_children() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < container_count; i++) {
            if (containers[i].pid == pid) {
                strcpy(containers[i].state, "stopped");
                containers[i].exit_status = status;
                printf("[Supervisor] Container %s exited\n", containers[i].id);
            }
        }
    }
}

// =========================================
// Supervisor
// =========================================
void run_supervisor() {
    printf("[Supervisor] Running...\n");

    // Start 2 containers automatically (for Task 1 demo)
    create_container("alpha", "./rootfs-alpha");
    create_container("beta", "./rootfs-beta");

    while (1) {
        reap_children();
        sleep(1);
    }
}

// =========================================
// Main
// =========================================
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./engine supervisor\n");
        return 1;
    }
    if (strcmp(argv[1], "supervisor") == 0) {
        run_supervisor();
    } else {
        printf("Invalid command\n");
    }
    return 0;
}