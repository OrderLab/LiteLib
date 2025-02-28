#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    time_t current_time;
    struct tm *time_info;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        sleep(2);
    }
        
        // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
        
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock_fd);
        sleep(2);
    }
        
        // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock_fd);
        sleep(2);
    }
    while (1) {
        // Create socket
        
        
        // Prepare message with current time
        time(&current_time);
        time_info = localtime(&current_time);
        snprintf(buffer, BUFFER_SIZE, "Hello, server! Time: %02d:%02d:%02d\n",
                time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
        
        // Send message
        write(sock_fd, buffer, strlen(buffer));
        printf("Sent: %s", buffer);
        
        // Receive response
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_read = read(sock_fd, buffer, BUFFER_SIZE);
        if (bytes_read > 0) {
            printf("Server response: %s", buffer);
        }
       
        sleep(2);  // Wait 2 seconds before next request
    } 
    close(sock_fd);
    
    return 0;
}
