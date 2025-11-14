// Andy Lim and Ifunaya Okafor
// Server for Project 3 Question 1
// Multi-threaded TCP server that reverses strings sent by clients

// Enable GNU-specific extensions (for pthread functions on some systems)
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Error number definitions and errno variable
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
// Internet address conversion functions
#include <arpa/inet.h>
// Internet protocol structures
#include <netinet/in.h>
// Socket functions and definitions
#include <sys/socket.h>

// Define the server port number (8080)
#define SRVPORT 8080
// Define buffer size for sending/receiving data (1024 bytes)
#define BUFSZ   1024

// Mutex for thread-safe console output
// static: limits visibility to this file only
// PTHREAD_MUTEX_INITIALIZER: initializes the mutex at compile time
static pthread_mutex_t cout_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread function to handle each client connection
// This function runs in a separate thread for each client
// arg: pointer to the socket file descriptor (passed from main)
// Returns: NULL (standard for pthread functions)
static void *processConn(void *arg)
{
    // Extract the socket file descriptor from the argument
    // Cast void* to int*, then dereference to get the actual value
    int newSkt = *(int *)arg;
    
    // Free the malloc'd memory (was allocated in main before creating thread)
    // Important to prevent memory leak
    free(arg);
    
    // Buffer to store received data (read buffer)
    char rbuf[BUFSZ];
    // Buffer to store data to send back (write buffer)
    char wbuf[BUFSZ];
    // Variable to store return value from recv() (bytes read)
    ssize_t n;

    // Initialize read buffer to all zeros
    memset(rbuf, 0, sizeof(rbuf));
    // Initialize write buffer to all zeros
    memset(wbuf, 0, sizeof(wbuf));

    // Lock mutex before printing (prevents interleaved output from multiple threads)
    pthread_mutex_lock(&cout_mutex);
    // Print thread ID and socket number to show connection accepted
    // pthread_self() returns the calling thread's ID
    printf("Thread %lu: Got new connection (socket %d)\n",
           (unsigned long)pthread_self(), newSkt);
    // Unlock mutex after printing
    pthread_mutex_unlock(&cout_mutex);

    // Read data from client (recv is thread-safe, blocks until data arrives)
    // newSkt: socket to read from
    // rbuf: buffer to store received data
    // BUFSZ - 1: max bytes to read (leave space for null terminator)
    // 0: flags (no special behavior)
    // Returns: number of bytes read, 0 if connection closed, -1 on error
    n = recv(newSkt, rbuf, BUFSZ - 1, 0);
    
    // Check if recv() returned error (-1)
    if (n < 0) {
        // Lock mutex for thread-safe printing
        pthread_mutex_lock(&cout_mutex);
        // Print error message with system error description
        printf("Error reading from socket: %s\n", strerror(errno));
        pthread_mutex_unlock(&cout_mutex);
        // Close the socket
        close(newSkt);
        // Exit thread (return NULL as required by pthread)
        return NULL;
    }
    
    // Check if client closed connection gracefully (recv returns 0)
    if (n == 0) {
        pthread_mutex_lock(&cout_mutex);
        printf("Client disconnected\n");
        pthread_mutex_unlock(&cout_mutex);
        close(newSkt);
        return NULL;
    }

    // Add null terminator to make received data a valid C string
    rbuf[n] = '\0';

    // Print received data (thread-safe with mutex)
    pthread_mutex_lock(&cout_mutex);
    printf("Thread %lu: Read from socket: %s\n", (unsigned long)pthread_self(), rbuf);
    // %zd is the correct format specifier for ssize_t
    printf("Length: %zd\n", n);
    pthread_mutex_unlock(&cout_mutex);

    // Reverse the string
    // Loop through each character in the received string
    for (ssize_t i = 0; i < n; i++) {
        // Copy character from end of rbuf to start of wbuf
        // When i=0, copy rbuf[n-1] (last char) to wbuf[0] (first char)
        // When i=1, copy rbuf[n-2] to wbuf[1], etc.
        wbuf[i] = rbuf[n - 1 - i];
    }
    // Add null terminator to make reversed string valid
    wbuf[n] = '\0';

    // Print the reversed string (thread-safe)
    pthread_mutex_lock(&cout_mutex);
    printf("Thread %lu: Reversed string: %s\n", (unsigned long)pthread_self(), wbuf);
    pthread_mutex_unlock(&cout_mutex);

    // Send response back to client (handle partial sends)
    // Calculate total bytes to send
    ssize_t to_send = (ssize_t)strlen(wbuf);
    // Track how many bytes have been sent
    ssize_t sent = 0;
    
    // Loop until all bytes are sent
    // (send() might not send all bytes in one call)
    while (sent < to_send) {
        // Send remaining bytes
        // wbuf + sent: pointer to unsent portion
        // to_send - sent: number of bytes remaining
        // Returns: number of bytes actually sent (or -1 on error)
        ssize_t m = send(newSkt, wbuf + sent, (size_t)(to_send - sent), 0);
        
        // Check if send failed
        if (m < 0) {
            pthread_mutex_lock(&cout_mutex);
            printf("Error writing to socket: %s\n", strerror(errno));
            pthread_mutex_unlock(&cout_mutex);
            // Break out of loop on error
            break;
        }
        // Update count of bytes sent
        sent += m;
    }

    // Close the socket (cleanup)
    close(newSkt);
    // Exit thread
    return NULL;
}

// Main function - sets up server and accepts connections
int main(void)
{
    // Server socket file descriptor
    int srvFd;
    // Structure to hold server address information
    struct sockaddr_in addr;

    // Create a TCP socket
    // AF_INET: IPv4
    // SOCK_STREAM: TCP (reliable, connection-oriented)
    // 0: default protocol (TCP)
    srvFd = socket(AF_INET, SOCK_STREAM, 0);
    if (srvFd < 0) {
        fprintf(stderr, "Creation of socket failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Set SO_REUSEADDR option
    // Allows reusing the port immediately after server restart
    // Without this, you'd get "Address already in use" error
    int opt = 1;
    if (setsockopt(srvFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "setsockopt failed: %s\n", strerror(errno));
        close(srvFd);
        exit(EXIT_FAILURE);
    }

    // Initialize address structure to zeros
    memset(&addr, 0, sizeof(addr));
    // Set address family to IPv4
    addr.sin_family = AF_INET;
    // Bind to all available network interfaces
    // INADDR_ANY = 0.0.0.0 (accept connections on any interface)
    // htonl: convert from host byte order to network byte order (32-bit)
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // Set port number (convert to network byte order)
    addr.sin_port = htons(SRVPORT);

    // Bind socket to the address and port
    // Associates the socket with the specified IP and port
    if (bind(srvFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Binding of socket failed: %s\n", strerror(errno));
        close(srvFd);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    // srvFd: socket to listen on
    // 10: backlog (max number of pending connections in queue)
    if (listen(srvFd, 10) < 0) {
        fprintf(stderr, "Listening on socket failed: %s\n", strerror(errno));
        close(srvFd);
        exit(EXIT_FAILURE);
    }

    // Inform user that server is ready
    printf("Server listening on port %d...\n", SRVPORT);
    printf("Press Ctrl+C to stop the server\n\n");

    // Main server loop - accept connections forever
    // for(;;) is equivalent to while(1) - infinite loop
    for (;;) {
        // Structure to store client's address information
        struct sockaddr_in client_addr;
        // Length of client address structure
        socklen_t alen = sizeof(client_addr);
        
        // Allocate memory for socket descriptor (to pass to thread)
        // Each thread needs its own copy of the socket descriptor
        int *newSkt = malloc(sizeof(int));
        if (!newSkt) {
            fprintf(stderr, "malloc failed\n");
            break;
        }

        // Accept incoming connection (blocks until client connects)
        // srvFd: listening socket
        // client_addr: filled with client's address information
        // alen: size of client_addr structure
        // Returns: new socket for communicating with this client
        *newSkt = accept(srvFd, (struct sockaddr *)&client_addr, &alen);
        
        // Check if accept failed
        if (*newSkt < 0) {
            fprintf(stderr, "accept failed: %s\n", strerror(errno));
            free(newSkt);
            // Continue to next iteration (don't exit, just skip this connection)
            continue;
        }

        // Convert client IP address from binary to readable string
        char ipstr[INET_ADDRSTRLEN]; // Buffer to hold IP string (xxx.xxx.xxx.xxx)
        // inet_ntop: "network to presentation" - converts binary IP to string
        inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
        
        // Get client's port number (convert from network to host byte order)
        uint16_t cport = ntohs(client_addr.sin_port);

        // Print connection information (thread-safe)
        pthread_mutex_lock(&cout_mutex);
        printf("\n=== New connection from %s:%u ===\n", ipstr, (unsigned)cport);
        pthread_mutex_unlock(&cout_mutex);

        // Create a new thread to handle this client connection
        pthread_t tid; // Thread ID (not used after creation)
        pthread_attr_t attr; // Thread attributes
        
        // Initialize thread attributes to default values
        pthread_attr_init(&attr);
        
        // Set thread to detached state
        // Detached threads automatically release resources when they exit
        // No need to call pthread_join()
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        
        // Create the thread
        // tid: receives the thread ID (not used here)
        // &attr: use our custom attributes (detached)
        // processConn: function to run in the thread
        // newSkt: argument to pass to processConn
        if (pthread_create(&tid, &attr, processConn, newSkt) != 0) {
            // Thread creation failed
            pthread_mutex_lock(&cout_mutex);
            printf("pthread_create failed: %s\n", strerror(errno));
            pthread_mutex_unlock(&cout_mutex);
            // Clean up
            close(*newSkt);
            free(newSkt);
        }
        // Destroy thread attributes (cleanup)
        pthread_attr_destroy(&attr);
        
        
    }

    // Close server socket (only reached if loop breaks, which it doesn't in this code)
    close(srvFd);
    return 0;
}
