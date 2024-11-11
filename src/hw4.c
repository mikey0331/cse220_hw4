#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024

int main() {
    int server_fd1;
    struct sockaddr_in address1;
    int opt = 1;
    int addrlen = sizeof(address1);
    
    // Create socket file descriptor
    if ((server_fd1 = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd1, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    // Setup address structure
    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PORT1);
    
    // Bind socket
    if (bind(server_fd1, (struct sockaddr *)&address1, sizeof(address1)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd1, 1) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Accept connection
    int client_socket = accept(server_fd1, (struct sockaddr *)&address1, (socklen_t*)&addrlen);
    if (client_socket < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    // Basic response to test connection
    char response[] = "OK\n";
    send(client_socket, response, strlen(response), 0);
    
    close(client_socket);
    close(server_fd1);
    
    return 0;
}