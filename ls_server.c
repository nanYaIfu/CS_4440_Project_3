
// Andy Lim and Ifunaya Okafor
// Server for Project 3 Question 2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// Port number for ls server
#define LS_PORT 8083

// Function to connect to the ls server
// Returns: socket descriptor on success, -1 on failure
int connectToLS(const char* server_ip) {
    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    // Check if socket creation failed
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Structure to hold server address information
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(LS_PORT);
    
    // Convert IP address from string to binary format
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        close(sock);
        return -1;
    }
    
    // Attempt to connect to the server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed - is ls server running?\n");
        close(sock);
        return -1;
    }
    
    // Connection successful
    return sock;
}

// Main function - entry point of program
int main(int argc, char* argv[]) {
    // Check if user provided server IP address
    if (argc < 2) {
        printf("Usage: %s <server_ip> [ls options]\n", argv[0]);
        printf("Examples:\n");
        printf("  %s 127.0.0.1\n", argv[0]);
        printf("  %s 127.0.0.1 -l\n", argv[0]);
        printf("  %s 127.0.0.1 -la /tmp\n", argv[0]);
        printf("  %s 127.0.0.1 -lh /home\n", argv[0]);
        return 1;
    }
    
    // Store server IP address
    const char* server_ip = argv[1];
    
    // Buffer to build ls command string
    char ls_cmd[1024] = "";
    
    // Build command string from remaining arguments
    for (int i = 2; i < argc; i++) {
        if (i > 2) strcat(ls_cmd, " ");
        strcat(ls_cmd, argv[i]);
    }
    
    // Connect to the ls server
    int sock = connectToLS(server_ip);
    
    // Check if connection failed
    if (sock < 0) {
        return 1;
    }
    
    // Send ls command to server
    send(sock, ls_cmd, strlen(ls_cmd), 0);
    
    // Buffer to receive data from server
    char buffer[8192];
    ssize_t bytes_read;
    
    // Read loop - keep reading until no more data
    while ((bytes_read = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    
    // Close the socket
    close(sock);
    
    return 0;
}
