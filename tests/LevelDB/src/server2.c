#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define UNIX_SOCKET_PATH "/tmp/socket_share.sock"

int recv_fd(int unix_sock) {
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))] = {0};
    char dummy;
    struct iovec io = { .iov_base = &dummy, .iov_len = sizeof(dummy) };
    int fd;

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    if (recvmsg(unix_sock, &msg, 0) < 0) {
        perror("recvmsg");
        return -1;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(client_fd, buffer, BUFFER_SIZE)) > 0) {
        printf("Received: %s", buffer);
        write(client_fd, buffer, bytes_read);
        memset(buffer, 0, BUFFER_SIZE);
    }

    close(client_fd);
}

int main() {
    int unix_sock;
    struct sockaddr_un unix_addr;

    unix_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_sock < 0) {
        perror("Unix socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    strncpy(unix_addr.sun_path, UNIX_SOCKET_PATH, sizeof(unix_addr.sun_path) - 1);

    if (connect(unix_sock, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) < 0) {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to process 1. Waiting for client connections...\n");

    while (1) {
        // Receive client socket from process 1
        int client_fd = recv_fd(unix_sock);
        if (client_fd < 0) {
            perror("Failed to receive client fd");
            continue;
        }

        printf("Received client socket. Handling client...\n");
        handle_client(client_fd);
    }

    close(unix_sock);
    return 0;
}
