// Andy Lim and Ifunaya Okafor
// Client for Project 3 Question 1

// Enable GNU extensions for additional POSIX features
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Define the server port number (8080)
#define CLNPORT 8080
// Define buffer size for receiving data (1024 bytes)
#define BUFSZ   1024

// Main function - entry point of the program
// argc: argument count, argv: argument vector (array of strings)
int main(int argc, char const *argv[])
{
    // Check if user provided correct number of arguments
    // Need 3 arguments: program name, server IP, message string
    if (argc < 3) {
        // Print usage message to standard error
        fprintf(stderr, "Usage: %s <srvaddr> <string>\n", argv[0]);
        // Print example usage
        fprintf(stderr, "Example: %s 127.0.0.1 \"Hello World\"\n", argv[0]);
        // Return 1 to indicate error
        return 1;
    }

    // Print the server IP address from command line argument
    printf("IP: %s\n", argv[1]);
    // Print the message string from command line argument
    printf("String: %s\n", argv[2]);

    // Create a TCP socket
    // AF_INET: IPv4 protocol family
    // SOCK_STREAM: TCP (connection-oriented, reliable)
    // 0: Use default protocol (TCP for SOCK_STREAM)
    // Returns: socket file descriptor (or -1 on error)
    int clnSck = socket(AF_INET, SOCK_STREAM, 0);
    
    // Check if socket creation failed
    if (clnSck < 0) {
        // Print error message with system error description
        fprintf(stderr, "Error: socket create error: %s\n", strerror(errno));
        // Return 1 to indicate error
        return 1;
    }

    // Declare structure to hold server address information
    struct sockaddr_in saddr;
    
    // Initialize the structure to all zeros
    // This ensures no garbage values in any fields
    memset(&saddr, 0, sizeof(saddr));
    
    // Set address family to IPv4
    saddr.sin_family = AF_INET;
    
    // Set port number (convert from host byte order to network byte order)
    // htons: "host to network short" - converts 16-bit integer
    saddr.sin_port = htons(CLNPORT);

    // Convert IP address from string (e.g., "127.0.0.1") to binary format
    // argv[1]: source IP string
    // &saddr.sin_addr: destination binary address
    // Returns: 1 on success, 0 if invalid format, -1 on error
    if (inet_pton(AF_INET, argv[1], &saddr.sin_addr) <= 0) {
        // Print error message
        fprintf(stderr, "Invalid address / Address not supported\n");
        // Close the socket before exiting
        close(clnSck);
        // Return 1 to indicate error
        return 1;
    }

    // Attempt to connect to the server
    // clnSck: client socket file descriptor
    // (struct sockaddr *)&saddr: pointer to server address (cast to generic sockaddr)
    // sizeof(saddr): size of address structure
    // Returns: 0 on success, -1 on error
    if (connect(clnSck, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        // Print error message with system error description
        fprintf(stderr, "Connect to server failed: %s\n", strerror(errno));
        // Close the socket before exiting
        close(clnSck);
        // Return 1 to indicate error
        return 1;
    }

    // Connection successful - inform user
    printf("Connected to server successfully\n");

    // Send the whole message (handle short writes)
    // Store pointer to the message string
    const char *msg = argv[2];
    
    // Calculate total bytes to send (length of string)
    size_t to_send = strlen(msg);
    
    // Track how many bytes have been sent so far (start at 0)
    size_t sent = 0;

    // Loop until all bytes are sent
    // (send() may not send all bytes in one call)
    while (sent < to_send) {
        // Send remaining bytes
        // msg + sent: pointer to unsent portion of message
        // to_send - sent: number of bytes remaining
        // 0: flags (no special behavior)
        // Returns: number of bytes actually sent (or -1 on error)
        ssize_t n = send(clnSck, msg + sent, to_send - sent, 0);
        
        // Check if send failed
        if (n < 0) {
            // Print error message
            fprintf(stderr, "Error sending: %s\n", strerror(errno));
            // Close socket before exiting
            close(clnSck);
            // Return 1 to indicate error
            return 1;
        }
        
        // Update count of bytes sent
        sent += (size_t)n;
    }

    // All bytes sent successfully - inform user
    printf("Message sent: %s\n", msg);

    // Read one reply (server replies once)
    // Declare buffer to store received data
    char rbuff[BUFSZ];
    
    // Receive data from server
    // clnSck: socket to receive from
    // rbuff: buffer to store received data
    // BUFSZ - 1: maximum bytes to receive (leave room for null terminator)
    // 0: flags (no special behavior)
    // Returns: number of bytes received, 0 if connection closed, -1 on error
    ssize_t rcvd = recv(clnSck, rbuff, BUFSZ - 1, 0);
    
    // Check if data was received
    if (rcvd > 0) {
        // Add null terminator to make it a valid C string
        rbuff[rcvd] = '\0';
        // Print the received message
        printf("Message read: %s\n", rbuff);
        
    // Check if server closed connection (graceful shutdown)
    } else if (rcvd == 0) {
        // Inform user that connection was closed
        printf("Server closed the connection without a reply\n");
        
    // recv() returned -1, indicating an error
    } else {
        // Print error message with system error description
        fprintf(stderr, "Error reading: %s\n", strerror(errno));
    }

    // Close the socket (cleanup)
    close(clnSck);
    
   
    return 0;
}
