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

#define STACK_SIZE (1024 * 1024)
#define MAX_CONTAINERS 10
#define SOCKET_PATH "/tmp/container_socket"

// =========================================
// Container Metadata
// =========================================
typedef struct {
    char id[32];
    pid_t pid;
    char state[16];
    char rootfs[128];
    time_t start_time;
} container_t;

container_t containers[MAX_CONTAINERS];
int container_count = 0;

char stack[STACK_SIZE];

// =========================================
// Container Code
// =========================================
int container_main(void *arg) {
    char *rootfs = (char *)arg;
    sethostname("container", 9);
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    if (chroot(rootfs) != 0) {
        perror("chroot failed");
        return -1;
    }
    chdir("/");
    mkdir("/proc", 0555);
    mount("proc", "/proc", "proc", 0, NULL);
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
    strcpy(containers[container_count].id, id);
    containers[container_count].pid = pid;
    strcpy(containers[container_count].state, "running");
    strcpy(containers[container_count].rootfs, rootfs);
    containers[container_count].start_time = time(NULL);
    container_count++;
    printf("[Supervisor] Started %s (PID %d)\n", id, pid);
    return pid;
}

// =========================================
// Reap Zombies
// =========================================
void reap_children(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < container_count; i++) {
            if (containers[i].pid == pid) {
                strcpy(containers[i].state, "stopped");
                printf("[Supervisor] %s exited\n", containers[i].id);
            }
        }
    }
}

// =========================================
// Handle Client Commands
// =========================================
void handle_client(int client_fd) {
    char buffer[256] = {0};
    read(client_fd, buffer, sizeof(buffer));
    printf("[Supervisor] Command: %s\n", buffer);

    char cmd[16], id[32], rootfs[128], exec_cmd[128];
    sscanf(buffer, "%s %s %s %s", cmd, id, rootfs, exec_cmd);

    // START
    if (strcmp(cmd, "start") == 0) {
        create_container(id, rootfs);
        write(client_fd, "Started\n", 8);
    }
    // RUN (foreground)
    else if (strcmp(cmd, "run") == 0) {
        pid_t pid = create_container(id, rootfs);
        waitpid(pid, NULL, 0);
        write(client_fd, "Run complete\n", 13);
    }
    // PS
    else if (strcmp(cmd, "ps") == 0) {
        char out[512] = {0};
        for (int i = 0; i < container_count; i++) {
            char line[128];
            sprintf(line, "%s | PID=%d | %s\n",
                    containers[i].id,
                    containers[i].pid,
                    containers[i].state);
            strcat(out, line);
        }
        write(client_fd, out, strlen(out));
    }
    // STOP
    else if (strcmp(cmd, "stop") == 0) {
        for (int i = 0; i < container_count; i++) {
            if (strcmp(containers[i].id, id) == 0) {
                kill(containers[i].pid, SIGTERM);
                strcpy(containers[i].state, "stopped");
            }
        }
        write(client_fd, "Stopped\n", 8);
    }
    // LOGS (basic placeholder)
    else if (strcmp(cmd, "logs") == 0) {
        write(client_fd, "Logs not implemented yet\n", 25);
    }
    else {
        write(client_fd, "Invalid command\n", 16);
    }
    close(client_fd);
}

// =========================================
// Socket Server
// =========================================
void setup_socket() {
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    unlink(SOCKET_PATH);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);
    printf("[Supervisor] Listening on %s\n", SOCKET_PATH);
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        handle_client(client_fd);
    }
}

// =========================================
// Graceful Shutdown
// =========================================
void handle_sigint(int sig) {
    printf("\n[Supervisor] Shutting down...\n");
    for (int i = 0; i < container_count; i++) {
        kill(containers[i].pid, SIGTERM);
    }
    unlink(SOCKET_PATH);
    exit(0);
}

// =========================================
// Supervisor
// =========================================
void run_supervisor() {
    printf("[Supervisor] Running...\n");
    signal(SIGCHLD, reap_children);
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    setup_socket();
}

// =========================================
// Main
// =========================================
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./engine [command]\n");
        return 1;
    }
    // Supervisor mode
    if (strcmp(argv[1], "supervisor") == 0) {
        run_supervisor();
    }
    // CLI mode
    else {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, SOCKET_PATH);
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("connect failed");
            return 1;
        }
        char msg[256] = {0};
        for (int i = 1; i < argc; i++) {
            strcat(msg, argv[i]);
            strcat(msg, " ");
        }
        write(sock, msg, strlen(msg));
        char response[512] = {0};
        read(sock, response, sizeof(response));
        printf("%s\n", response);
        close(sock);
    }
    return 0;
}