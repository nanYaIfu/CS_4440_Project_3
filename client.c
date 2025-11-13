// Andy Lim and Ifunaya Okafor
// Client for Project 3 Question 1

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define CLNPORT 8080
#define BUFSZ   1024

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <srvaddr> <string>\n", argv[0]);
        fprintf(stderr, "Example: %s 127.0.0.1 \"Hello World\"\n", argv[0]); // can change the ip address
        return 1;
    }

    printf("IP: %s\n", argv[1]);
    printf("String: %s\n", argv[2]);

    int clnSck = socket(AF_INET, SOCK_STREAM, 0);
    if (clnSck < 0) {
        fprintf(stderr, "Error: socket create error: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(CLNPORT);

    if (inet_pton(AF_INET, argv[1], &saddr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address / Address not supported\n");
        close(clnSck);
        return 1;
    }

    if (connect(clnSck, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        fprintf(stderr, "Connect to server failed: %s\n", strerror(errno));
        close(clnSck);
        return 1;
    }
    printf("Connected to server successfully\n");

    // Send the whole message (handle short writes)
    const char *msg = argv[2];
    size_t to_send = strlen(msg);
    size_t sent = 0;
    while (sent < to_send) {
        ssize_t n = send(clnSck, msg + sent, to_send - sent, 0);
        if (n < 0) {
            fprintf(stderr, "Error sending: %s\n", strerror(errno));
            close(clnSck);
            return 1;
        }
        sent += (size_t)n;
    }
    printf("Message sent: %s\n", msg);

    // Read one reply (server replies once)
    char rbuff[BUFSZ];
    ssize_t rcvd = recv(clnSck, rbuff, BUFSZ - 1, 0);
    if (rcvd > 0) {
        rbuff[rcvd] = '\0';
        printf("Message read: %s\n", rbuff);
    } else if (rcvd == 0) {
        printf("Server closed the connection without a reply\n");
    } else {
        fprintf(stderr, "Error reading: %s\n", strerror(errno));
    }

    close(clnSck);
    return 0;
}
