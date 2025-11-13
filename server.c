// Andy Lim and Ifunaya Okafor
// Client for Project 3 Question 1

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

static pthread_mutex_t cout_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *processConn(void *arg)
{
    int newSkt = *(int *)arg;
    free(arg); // allocated in main
    char rbuf[BUFSZ];
    char wbuf[BUFSZ];
    ssize_t n;

    memset(rbuf, 0, sizeof(rbuf));
    memset(wbuf, 0, sizeof(wbuf));

    pthread_mutex_lock(&cout_mutex);
    printf("Thread %lu: Got new connection (socket %d)\n",
           (unsigned long)pthread_self(), newSkt);
    pthread_mutex_unlock(&cout_mutex);

    // Read once (simple echo-style protocol)
    n = recv(newSkt, rbuf, BUFSZ - 1, 0); // leave space for '\0'
    if (n < 0) {
        pthread_mutex_lock(&cout_mutex);
        printf("Error reading from socket: %s\n", strerror(errno));
        pthread_mutex_unlock(&cout_mutex);
        close(newSkt);
        return NULL;
    }
    if (n == 0) {
        pthread_mutex_lock(&cout_mutex);
        printf("Client disconnected\n");
        pthread_mutex_unlock(&cout_mutex);
        close(newSkt);
        return NULL;
    }

    rbuf[n] = '\0';

    pthread_mutex_lock(&cout_mutex);
    printf("Thread %lu: Read from socket: %s\n", (unsigned long)pthread_self(), rbuf);
    printf("Length: %zd\n", n);
    pthread_mutex_unlock(&cout_mutex);

    // Reverse the string (do not include the trailing '\0')
    for (ssize_t i = 0; i < n; i++) {
        wbuf[i] = rbuf[n - 1 - i];
    }
    wbuf[n] = '\0';

    pthread_mutex_lock(&cout_mutex);
    printf("Thread %lu: Reversed string: %s\n", (unsigned long)pthread_self(), wbuf);
    pthread_mutex_unlock(&cout_mutex);

    // Send response
    ssize_t to_send = (ssize_t)strlen(wbuf);
    ssize_t sent = 0;
    while (sent < to_send) {
        ssize_t m = send(newSkt, wbuf + sent, (size_t)(to_send - sent), 0);
        if (m < 0) {
            pthread_mutex_lock(&cout_mutex);
            printf("Error writing to socket: %s\n", strerror(errno));
            pthread_mutex_unlock(&cout_mutex);
            break;
        }
        sent += m;
    }

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
        fprintf(stderr, "Creation of socket failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Reuse address
    int opt = 1;
    if (setsockopt(srvFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "setsockopt failed: %s\n", strerror(errno));
        close(srvFd);
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SRVPORT);

    // Bind
    if (bind(srvFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Binding of socket failed: %s\n", strerror(errno));
        close(srvFd);
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(srvFd, 10) < 0) {
        fprintf(stderr, "Listening on socket failed: %s\n", strerror(errno));
        close(srvFd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SRVPORT);
    printf("Press Ctrl+C to stop the server\n\n");

    // Accept loop
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t alen = sizeof(client_addr);
        int *newSkt = malloc(sizeof(int));
        if (!newSkt) {
            fprintf(stderr, "malloc failed\n");
            break;
        }

        *newSkt = accept(srvFd, (struct sockaddr *)&client_addr, &alen);
        if (*newSkt < 0) {
            fprintf(stderr, "accept failed: %s\n", strerror(errno));
            free(newSkt);
            continue;
        }

        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
        uint16_t cport = ntohs(client_addr.sin_port);

        pthread_mutex_lock(&cout_mutex);
        printf("\n=== New connection from %s:%u ===\n", ipstr, (unsigned)cport);
        pthread_mutex_unlock(&cout_mutex);

        // Spawn detached thread
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, processConn, newSkt) != 0) {
            pthread_mutex_lock(&cout_mutex);
            printf("pthread_create failed: %s\n", strerror(errno));
            pthread_mutex_unlock(&cout_mutex);
            close(*newSkt);
            free(newSkt);
        }
        pthread_attr_destroy(&attr);
    }

    close(srvFd);
    return 0;
}
