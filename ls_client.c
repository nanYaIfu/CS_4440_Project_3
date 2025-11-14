// Andy Lim and Ifunaya Okafor
// Client for Project 3 Question 2
// Client that connects to ls_server to get directory listings

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
// Internet address functions
#include <arpa/inet.h>

// Port number for ls server (8083)
#define LS_PORT 8083

// Function to connect to the ls server
// server_ip: IP address of server as string (e.g., "127.0.0.1")
// Returns: socket file descriptor on success, -1 on failure
int connectToLS(const char* server_ip) {
    // Create a TCP socket
    // AF_INET: IPv4 protocol
    // SOCK_STREAM: TCP (connection-oriented)
    // 0: default protocol (TCP)
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    // Check if socket creation failed
    if (sock < 0) {
        // perror prints error message with system error description
        perror("socket");
        return -1;
    }
    
    // Structure to hold server address information
    struct sockaddr_in serv_addr;
    
    // Set address family to IPv4
    serv_addr.sin_family = AF_INET;
    
    // Set port number (convert from host to network byte order)
    serv_addr.sin_port = htons(LS_PORT);
    
    // Convert IP address from string to binary format
    // server_ip: source IP string (e.g., "127.0.0.1")
    // &serv_addr.sin_addr: destination for binary IP
    // Returns: 1 on success, 0 if invalid format, -1 on error
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        // Close socket before returning
        close(sock);
        return -1;
    }
    
    // Attempt to connect to the server
    // sock: socket to use for connection
    // (struct sockaddr*)&serv_addr: server address (cast to generic type)
    // sizeof(serv_addr): size of address structure
    // Returns: 0 on success, -1 on failure
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed - is ls server running?\n");
        close(sock);
        return -1;
    }
    
    // Connection successful, return socket descriptor
    return sock;
}

// Main function - entry point of program
// argc: number of command line arguments
// argv: array of argument strings
int main(int argc, char* argv[]) {
    // Check if user provided server IP address
    // Need at least 2 arguments: program name and server IP
    if (argc < 2) {
        // Print usage information
        printf("Usage: %s <server_ip> [ls options]\n", argv[0]);
        printf("Examples:\n");
        printf("  %s 127.0.0.1\n", argv[0]);
        printf("  %s 127.0.0.1 -l\n", argv[0]);
        printf("  %s 127.0.0.1 -la /tmp\n", argv[0]);
        printf("  %s 127.0.0.1 -lh /home\n", argv[0]);
        return 1;
    }
    
    // Store server IP address (first argument after program name)
    const char* server_ip = argv[1];
    
    // Buffer to build ls command string
    // Will contain all ls options and paths (e.g., "-la /tmp")
    char ls_cmd[1024] = ""; // Initialize to empty string
    
    // Build command string from remaining arguments
    // Start at index 2 (skip program name and server IP)
    for (int i = 2; i < argc; i++) {
        // Add space before each argument (except the first one)
        if (i > 2) strcat(ls_cmd, " ");
        
        // Append current argument to command string
        // strcat: string concatenation
        strcat(ls_cmd, argv[i]);
    }
    // Example: if args are ["-l", "/tmp"], ls_cmd becomes "-l /tmp"
    
    // Connect to the ls server
    int sock = connectToLS(server_ip);
    
    // Check if connection failed
    if (sock < 0) {
        return 1;
    }
    
    // Send ls command to server
    // sock: socket to send through
    // ls_cmd: command string to send
    // strlen(ls_cmd): number of bytes to send
    // 0: flags (no special options)
    send(sock, ls_cmd, strlen(ls_cmd), 0);
    
    // Buffer to receive data from server
    char buffer[8192]; // 8KB buffer for directory listing output
    
    // Variable to store number of bytes read
    ssize_t bytes_read;
    
    // Read loop - keep reading until no more data
    // read() returns number of bytes read, 0 when done, -1 on error
    while ((bytes_read = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        // Add null terminator to make buffer a valid C string
        // Use bytes_read as index (not sizeof(buffer))
        buffer[bytes_read] = '\0';
        
        // Print the received data (directory listing output)
        // %s format specifier for string
        printf("%s", buffer);
    }
    // Loop continues until:
    // - Server closes connection (read returns 0)
    // - Error occurs (read returns -1)
    // - All data received
    
    // Close the socket (cleanup)
    close(sock);
    
    return 0;
}