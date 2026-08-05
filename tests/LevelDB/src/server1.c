#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define UNIX_SOCKET_PATH "/tmp/socket_share.sock"

void send_fd(int unix_sock, int fd_to_send) {
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))] = {0};
    char dummy = 'x';
    struct iovec io = { .iov_base = &dummy, .iov_len = sizeof(dummy) };
    
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);
    
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    
    memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));
    
    if (sendmsg(unix_sock, &msg, 0) < 0) {
        perror("sendmsg");
        exit(1);
    }
}

int main() {
    int server_fd, unix_sock;
    struct sockaddr_in server_addr;
    struct sockaddr_un unix_addr;
    
    // Create TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("TCP bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // Create Unix domain socket
    unix_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_sock < 0) {
        perror("Unix socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    strncpy(unix_addr.sun_path, UNIX_SOCKET_PATH, sizeof(unix_addr.sun_path) - 1);

    unlink(UNIX_SOCKET_PATH);

    if (bind(unix_sock, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) < 0) {
        perror("Unix bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(unix_sock, 1) < 0) {
        perror("Unix listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for process 2 to connect...\n");
    int unix_client = accept(unix_sock, NULL, NULL);
    if (unix_client < 0) {
        perror("Unix accept failed");
        exit(EXIT_FAILURE);
    }
    printf("Process 2 connected. Accepting client connections...\n");

    // Accept client connections and pass them to process 2
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while(1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Client accept failed");
            continue;
        }

        printf("New client connected: %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));

        // Pass client socket to process 2
        send_fd(unix_client, client_fd);
        close(client_fd);  // Close our copy of the fd
    }

    return 0;
}
