// Andy Lim and Ifunaya Okafor
// Server for Project 3 Question 1

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SRVPORT 8080
#define BUFSZ   1024

// one mutex for printing
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

void *processConn(void *arg)
{
    int newSkt = *(int *)arg;
    free(arg);
    
    char rbuf[BUFSZ];
    char wbuf[BUFSZ];
    ssize_t n;

    memset(rbuf, 0, sizeof(rbuf));
    memset(wbuf, 0, sizeof(wbuf));

    pthread_mutex_lock(&print_mutex);
    printf("Got new connection on socket %d\n", newSkt);
    pthread_mutex_unlock(&print_mutex);

    // Read from client
    n = recv(newSkt, rbuf, BUFSZ - 1, 0);
    if (n <= 0) {
        printf("Error or client disconnected\n");
        close(newSkt);
        return NULL;
    }

    rbuf[n] = '\0';

    printf("Read: %s (length: %zd)\n", rbuf, n);

    // Reverse the string
    int i;
    for (i = 0; i < n; i++) {
        wbuf[i] = rbuf[n - 1 - i];
    }
    wbuf[n] = '\0';

    printf("Reversed: %s\n", wbuf);

    // Send back
    send(newSkt, wbuf, strlen(wbuf), 0);

    close(newSkt);
    return NULL;
}

int main(void)
{
    int srvFd;
    struct sockaddr_in addr;

    // Create socket
    srvFd = socket(AF_INET, SOCK_STREAM, 0);
    if (srvFd < 0) {
        printf("Socket creation failed\n");
        exit(1);
    }

    // Reuse address
    int opt = 1;
    setsockopt(srvFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SRVPORT);

    // Bind
    if (bind(srvFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Bind failed\n");
        exit(1);
    }

    // Listen
    if (listen(srvFd, 10) < 0) {
        printf("Listen failed\n");
        exit(1);
    }

    printf("Server listening on port %d\n", SRVPORT);
    printf("Press Ctrl+C to stop\n\n");

    // Accept loop
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t alen = sizeof(client_addr);
        
        int *newSkt = malloc(sizeof(int));
        if (!newSkt) {
            printf("malloc failed\n");
            continue;
        }

        *newSkt = accept(srvFd, (struct sockaddr *)&client_addr, &alen);
        if (*newSkt < 0) {
            printf("accept failed\n");
            free(newSkt);
            continue;
        }

        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        // thread creation (no attributes)
        pthread_t tid;
        if (pthread_create(&tid, NULL, processConn, newSkt) != 0) {
            printf("pthread_create failed\n");
            close(*newSkt);
            free(newSkt);
        }
        pthread_detach(tid);
    }

    close(srvFd);
    return 0;
}
