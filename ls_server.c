// Andy Lim and Ifunaya Okafor
// Server for Project 3 Question 2
// Multi-threaded server that executes 'ls' command via fork/exec

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
// Internet protocol structures
#include <netinet/in.h>
// Process control functions (wait, waitpid)
#include <sys/wait.h>

// Port number for ls server
#define LS_PORT 8083
// Maximum buffer size for data
#define MAX_BUFFER 8192

// Mutex for thread-safe console output
// Global variable shared by all threads
pthread_mutex_t cout_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread function to handle each client connection
// This function runs in a separate thread for each client
// arg: pointer to socket descriptor (malloc'd in main)
// Returns: NULL (standard for pthread functions)
void* handleClient(void* arg) {
    // Extract socket descriptor from argument
    int client_sock = *(int*)arg;
    
    // Free the malloc'd memory (prevents memory leak)
    // Memory was allocated in main() before thread creation
    free(arg);
    
    // Buffer to receive client's command
    char buffer[MAX_BUFFER];
    // Initialize buffer to all zeros
    memset(buffer, 0, sizeof(buffer));
    
    // Read ls command from client
    // read() blocks until data arrives or connection closes
    // Returns: bytes read, 0 if closed, -1 on error
    int n = read(client_sock, buffer, sizeof(buffer) - 1);
    
    // Check if read failed or client disconnected
    if (n <= 0) {
        // Close socket and exit thread
        close(client_sock);
        return NULL;
    }
    
    // Add null terminator to make it a valid C string
    buffer[n] = '\0';
    
    // Print received command (thread-safe with mutex)
    pthread_mutex_lock(&cout_mutex);
    printf("Received ls command: %s\n", buffer);
    pthread_mutex_unlock(&cout_mutex);
    
    // ===== PARSE ARGUMENTS =====
    // Array of pointers to hold command arguments
    // Maximum 64 arguments (ls with many options)
    char* args[64];
    
    // Argument count (starts at 0)
    int argc = 0;
    
    // First argument is always the program name
    args[argc++] = "ls";
    
    // Parse the buffer into tokens (split by space, tab, newline)
    // strtok modifies the original string (replaces delimiters with \0)
    // First call: pass the string to tokenize
    char* token = strtok(buffer, " \t\n");
    
    // Loop through all tokens
    while (token != NULL && argc < 63) {
        // Store pointer to this token
        args[argc++] = token;
        
        // Get next token (pass NULL to continue with same string)
        token = strtok(NULL, " \t\n");
    }
    // Example: "-l /tmp" becomes args = ["ls", "-l", "/tmp", NULL]
    
    // Last element must be NULL (required by execvp)
    args[argc] = NULL;
    
    // ===== CREATE PIPE FOR IPC =====
    // Pipe file descriptors: [0]=read end, [1]=write end
    int pipefd[2];
    
    // Create pipe for communication between processes
    // Returns: 0 on success, -1 on error
    if (pipe(pipefd) == -1) {
        // Pipe creation failed
        char* error = "Error creating pipe\n";
        send(client_sock, error, strlen(error), 0);
        close(client_sock);
        return NULL;
    }
    
    // ===== FORK CHILD PROCESS =====
    // Create a new process (child process is exact copy of parent)
    // Returns: 0 in child, child's PID in parent, -1 on error
    pid_t pid = fork();
    
    // Check if fork failed
    if (pid < 0) {
        // Fork failed - send error to client
        char* error = "Error forking process\n";
        send(client_sock, error, strlen(error), 0);
        // Close both ends of pipe
        close(pipefd[0]);
        close(pipefd[1]);
        close(client_sock);
        return NULL;
        
    } else if (pid == 0) {
        // ===== CHILD PROCESS CODE =====
        // This code ONLY runs in the child process
        
        // Close read end of pipe (child only writes)
        close(pipefd[0]);
        
        // Redirect stdout to pipe write end
        // dup2(new, old): makes 'old' refer to same file as 'new'
        // Now printf/ls output goes to pipe instead of terminal
        dup2(pipefd[1], STDOUT_FILENO);
        
        // Redirect stderr to pipe write end too
        // Error messages also go to pipe
        dup2(pipefd[1], STDERR_FILENO);
        
        // Close original write end (no longer needed after dup2)
        close(pipefd[1]);
        
        // Execute 'ls' command with parsed arguments
        // execvp: replaces current process with new program
        // "ls": program to execute
        // args: array of arguments (["ls", "-l", "/tmp", NULL])
        // This function ONLY RETURNS if it fails
        execvp("ls", args);
        
        // If we reach here, exec failed
        perror("execvp failed");
        // Exit child process with error code
        exit(1);
        
    } else {
        // ===== PARENT PROCESS CODE =====
        // This code ONLY runs in the parent (original) process
        
        // Close write end of pipe (parent only reads)
        close(pipefd[1]);
        
        // Buffer to store output from child process
        char output[MAX_BUFFER];
        
        // Number of bytes read in current read() call
        ssize_t bytes_read;
        
        // Total bytes read so far
        ssize_t total_read = 0;
        
        // Read loop - get all output from child's ls command
        // read() from pipe blocks until data available or pipe closed
        while ((bytes_read = read(pipefd[0], output + total_read, 
                                  sizeof(output) - total_read - 1)) > 0) {
            // Accumulate total bytes read
            total_read += bytes_read;
        }
        // Loop ends when:
        // - Child closes pipe (child exits)
        // - Error occurs (bytes_read < 0)
        // - Buffer full
        
        // Close read end of pipe (done reading)
        close(pipefd[0]);
        
        // Wait for child process to finish
        // Prevents zombie processes (processes that finished but still in process table)
        int status;  // Will store child's exit status
        // waitpid blocks until child 'pid' finishes
        // status: filled with exit code/signal info
        // 0: no special options
        waitpid(pid, &status, 0);
        
        // Check if no output was produced
        if (total_read == 0) {
            // Either directory empty or error occurred
            strcpy(output, "(empty or error)\n");
            total_read = strlen(output);
        } else {
            // Add null terminator to output string
            output[total_read] = '\0';
        }
        
        // Send ls output back to client
        // output: contains ls command results
        // total_read: number of bytes to send
        send(client_sock, output, total_read, 0);
        
        // Log how much data was sent (thread-safe)
        pthread_mutex_lock(&cout_mutex);
        printf("Sent %zd bytes to client\n", total_read);
        pthread_mutex_unlock(&cout_mutex);
    }
    
    // Close client socket (cleanup)
    close(client_sock);
    
    // Exit thread
    return NULL;
}

// Main function - sets up server and accepts connections
int main(int argc, char* argv[]) {
    // Print server information banner
    printf("========================================\n");
    printf("Directory Listing Server (ls)\n");
    printf("========================================\n");
    printf("Port: %d\n", LS_PORT);
    printf("Uses fork() and execvp() to run ls\n");
    printf("========================================\n\n");
    
    // Create TCP socket
    // AF_INET: IPv4, SOCK_STREAM: TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Set SO_REUSEADDR option
    // Allows reusing the address/port immediately after restart
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set up server address structure
    struct sockaddr_in address;
    address.sin_family = AF_INET;           // IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // Accept on any interface
    address.sin_port = htons(LS_PORT);      // Port 8083
    
    // Bind socket to address and port
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }
    
    // Start listening for connections
    // 10: maximum queue of pending connections
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }
    
    // Server is ready
    printf("Directory listing server listening on port %d...\n", LS_PORT);
    printf("Press Ctrl+C to stop\n\n");
    
    // Main accept loop - run forever
    while (1) {
        // Structure to hold client address
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept incoming connection (blocks until client connects)
        int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        // Check if accept failed
        if (client_sock < 0) {
            // Skip this connection and try again
            continue;
        }
        
        // Allocate memory for socket descriptor
        // Each thread needs its own copy
        int* sock_ptr = malloc(sizeof(int));
        *sock_ptr = client_sock;
        
        // Create new thread to handle this client
        pthread_t thread;
        if (pthread_create(&thread, NULL, handleClient, sock_ptr) != 0) {
            // Thread creation failed
            perror("pthread_create");
            free(sock_ptr);      // Free allocated memory
            close(client_sock);  // Close socket
            continue;            // Try next connection
        }
        
        // Detach thread (auto-cleanup when thread exits)
        pthread_detach(thread);
    }
    
    // Close server socket (never reached in this code)
    close(server_fd);
    return 0;
}